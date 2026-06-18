// Baza medycznych badan obrazowych - calosc w jednym pliku
#include <QApplication>
#include <QMainWindow>
#include <QDialog>
#include <QWidget>
#include <QSplitter>
#include <QTabWidget>
#include <QToolBar>
#include <QAction>
#include <QTableView>
#include <QSqlDatabase>
#include <QSqlQueryModel>
#include <QSqlQuery>
#include <QSqlError>
#include <QLineEdit>
#include <QDateEdit>
#include <QComboBox>
#include <QLabel>
#include <QScrollArea>
#include <QSlider>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QMenu>
#include <QFileDialog>
#include <QDir>
#include <QStringList>
#include <QAbstractItemView>
#include <QModelIndex>
#include <QImage>
#include <QPixmap>
#include <QWheelEvent>
#include <QDate>
#include <QDebug>
#include <cstring>
#include <utility>

#include <dcmtk/config/osconfig.h>
#include <dcmtk/dcmimgle/dcmimage.h>
#include <dcmtk/dcmjpeg/djdecode.h>
#include <dcmtk/dcmjpls/djdecode.h>

// tworzenie tabel
bool initDB()
{
    QSqlQuery q;

    bool ok = q.exec("CREATE TABLE IF NOT EXISTS patients ("
                     "id SERIAL PRIMARY KEY,"
                     "surname VARCHAR(100),"
                     "name VARCHAR(100),"
                     "birth_date DATE,"
                     "pesel VARCHAR(11),"
                     "sex VARCHAR(1))");
    if (!ok) {
        qDebug() << "Blad tworzenia tabeli patients:" << q.lastError().text();
        return false;
    }

    q.exec("CREATE TABLE IF NOT EXISTS studies ("
           "id SERIAL PRIMARY KEY,"
           "patient_id INTEGER REFERENCES patients(id),"
           "study_date DATE,"
           "type VARCHAR(50),"
           "description TEXT)");

    q.exec("CREATE TABLE IF NOT EXISTS images ("
           "id SERIAL PRIMARY KEY,"
           "study_id INTEGER REFERENCES studies(id),"
           "file_path TEXT)");

    return true;
}

// podglad DICOM
class DicomViewer : public QWidget
{
    Q_OBJECT
public:
    DicomViewer(QWidget *parent = nullptr)
        : QWidget(parent), current(0), dcmImage(nullptr), scaleFactor(1.0)
    {
        infoLabel = new QLabel("Brak zaladowanej serii", this);

        imageLabel = new QLabel(this);
        imageLabel->setAlignment(Qt::AlignCenter);

        scrollArea = new QScrollArea(this);
        scrollArea->setWidget(imageLabel);
        scrollArea->setWidgetResizable(false);
        scrollArea->setAlignment(Qt::AlignCenter);

        // suwak warstw (pionowy, po prawej)
        sliceSlider = new QSlider(Qt::Vertical, this);
        sliceSlider->setRange(0, 0);
        sliceSlider->setInvertedAppearance(true);  // gora = pierwsza warstwa
        sliceSlider->setEnabled(false);
        connect(sliceSlider, &QSlider::valueChanged, this, &DicomViewer::pokazWarstwe);

        windowSlider = new QSlider(Qt::Horizontal, this);
        windowSlider->setRange(1, 4000);
        windowSlider->setValue(400);
        levelSlider = new QSlider(Qt::Horizontal, this);
        levelSlider->setRange(-1000, 3000);
        levelSlider->setValue(40);

        connect(windowSlider, &QSlider::valueChanged, this, &DicomViewer::przerysuj);
        connect(levelSlider, &QSlider::valueChanged, this, &DicomViewer::przerysuj);

        QHBoxLayout *slidersLay = new QHBoxLayout();
        slidersLay->addWidget(new QLabel("W:"));
        slidersLay->addWidget(windowSlider);
        slidersLay->addWidget(new QLabel("L:"));
        slidersLay->addWidget(levelSlider);

        // obraz + suwak warstw obok siebie
        QHBoxLayout *centerLay = new QHBoxLayout();
        centerLay->addWidget(scrollArea);
        centerLay->addWidget(sliceSlider);

        QVBoxLayout *lay = new QVBoxLayout(this);
        lay->addWidget(infoLabel);
        lay->addLayout(centerLay);
        lay->addLayout(slidersLay);
    }

    ~DicomViewer()
    {
        if (dcmImage)
            delete dcmImage;
    }

    // lista plikow = kolejne warstwy
    void loadSeries(const QStringList &paths)
    {
        slices = paths;
        current = 0;
        scaleFactor = 1.0;
        if (slices.isEmpty()) {
            imageLabel->setText("Pusta seria");
            infoLabel->setText("Brak warstw");
            sliceSlider->setRange(0, 0);
            sliceSlider->setEnabled(false);
            return;
        }

        sliceSlider->blockSignals(true);
        sliceSlider->setRange(0, slices.size() - 1);
        sliceSlider->setValue(0);
        sliceSlider->blockSignals(false);
        sliceSlider->setEnabled(slices.size() > 1);

        pokazWarstwe(0);
    }

    // pojedynczy plik = seria z jedna warstwa
    void loadFile(const QString &path)
    {
        loadSeries(QStringList() << path);
    }

protected:
    void wheelEvent(QWheelEvent *event) override
    {
        if (slices.isEmpty()) {
            QWidget::wheelEvent(event);
            return;
        }

        if (event->modifiers() & Qt::ControlModifier) {
            // Ctrl + kolko = zoom
            if (event->angleDelta().y() > 0)
                scaleFactor *= 1.1;
            else
                scaleFactor *= 0.9;
            if (scaleFactor < 0.1) scaleFactor = 0.1;
            if (scaleFactor > 10.0) scaleFactor = 10.0;
            przerysuj();
        } else {
            // samo kolko = przewijanie warstw
            if (event->angleDelta().y() > 0)
                pokazWarstwe(current - 1);
            else
                pokazWarstwe(current + 1);
        }
    }

private slots:
    void przerysuj()
    {
        if (!dcmImage)
            return;

        // center = poziom, width = okno (z suwakow)
        double center = levelSlider->value();
        double width = windowSlider->value();
        dcmImage->setWindow(center, width);

        int w = (int)dcmImage->getWidth();
        int h = (int)dcmImage->getHeight();

        const uchar *pixelData = (const uchar *)dcmImage->getOutputData(8);
        if (!pixelData) {
            imageLabel->setText("Brak danych obrazu");
            return;
        }

        QImage img(w, h, QImage::Format_Grayscale8);
        for (int y = 0; y < h; y++) {
            memcpy(img.scanLine(y), pixelData + y * w, w);
        }

        QPixmap pix = QPixmap::fromImage(img);
        pix = pix.scaled(pix.size() * scaleFactor, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        imageLabel->setPixmap(pix);
        imageLabel->resize(pix.size());
    }

private:
    void pokazWarstwe(int idx)
    {
        if (idx < 0) idx = 0;
        if (idx > slices.size() - 1) idx = slices.size() - 1;
        current = idx;

        // ustaw suwak bez petli sygnalow
        sliceSlider->blockSignals(true);
        sliceSlider->setValue(current);
        sliceSlider->blockSignals(false);

        if (dcmImage) {
            delete dcmImage;
            dcmImage = nullptr;
        }

        dcmImage = new DicomImage(slices[current].toLocal8Bit().constData());
        if (dcmImage->getStatus() != EIS_Normal) {
            imageLabel->setText("Nie udalo sie wczytac pliku DICOM");
            delete dcmImage;
            dcmImage = nullptr;
            return;
        }

        infoLabel->setText(QString("Warstwa %1 / %2   (%3 x %4)   |   kolko = przewijanie, Ctrl+kolko = zoom")
                           .arg(current + 1).arg(slices.size())
                           .arg((int)dcmImage->getWidth()).arg((int)dcmImage->getHeight()));
        przerysuj();
    }

    QLabel *infoLabel;
    QLabel *imageLabel;
    QScrollArea *scrollArea;
    QSlider *windowSlider;
    QSlider *levelSlider;
    QSlider *sliceSlider;
    QStringList slices;
    int current;
    DicomImage *dcmImage;
    double scaleFactor;
};

// dialog pacjenta
class AddPatientDialog : public QDialog
{
    Q_OBJECT
public:
    // editId > 0 = edycja
    AddPatientDialog(QWidget *parent = nullptr, int editId = -1)
        : QDialog(parent), editId(editId)
    {
        setWindowTitle(editId > 0 ? "Edytuj pacjenta" : "Dodaj pacjenta");

        surnameEdit = new QLineEdit(this);
        nameEdit = new QLineEdit(this);
        dateEdit = new QDateEdit(this);
        dateEdit->setCalendarPopup(true);
        dateEdit->setDisplayFormat("yyyy-MM-dd");
        dateEdit->setDate(QDate(2000, 1, 1));
        peselEdit = new QLineEdit(this);
        sexCombo = new QComboBox(this);
        sexCombo->addItem("M");
        sexCombo->addItem("K");

        QFormLayout *form = new QFormLayout();
        form->addRow("Nazwisko:", surnameEdit);
        form->addRow("Imie:", nameEdit);
        form->addRow("Data urodzenia:", dateEdit);
        form->addRow("PESEL:", peselEdit);
        form->addRow("Plec:", sexCombo);

        QDialogButtonBox *buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &AddPatientDialog::zapisz);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        QVBoxLayout *lay = new QVBoxLayout(this);
        lay->addLayout(form);
        lay->addWidget(buttons);

        if (editId > 0)
            wczytajDane();
    }

private slots:
    void zapisz()
    {
        if (surnameEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, "Uwaga", "Nazwisko nie moze byc puste.");
            return;
        }

        QSqlQuery q;
        if (editId > 0) {
            q.prepare("UPDATE patients SET surname=:surname, name=:name, "
                      "birth_date=:bdate, pesel=:pesel, sex=:sex WHERE id=:id");
            q.bindValue(":id", editId);
        } else {
            q.prepare("INSERT INTO patients (surname, name, birth_date, pesel, sex) "
                      "VALUES (:surname, :name, :bdate, :pesel, :sex)");
        }
        q.bindValue(":surname", surnameEdit->text());
        q.bindValue(":name", nameEdit->text());
        q.bindValue(":bdate", dateEdit->date().toString("yyyy-MM-dd"));
        q.bindValue(":pesel", peselEdit->text());
        q.bindValue(":sex", sexCombo->currentText());

        if (!q.exec()) {
            QMessageBox::critical(this, "Blad",
                "Nie udalo sie zapisac pacjenta:\n" + q.lastError().text());
            return;
        }

        accept();
    }

private:
    void wczytajDane()
    {
        QSqlQuery q;
        q.prepare("SELECT surname, name, birth_date, pesel, sex FROM patients WHERE id=:id");
        q.bindValue(":id", editId);
        q.exec();
        if (q.next()) {
            surnameEdit->setText(q.value(0).toString());
            nameEdit->setText(q.value(1).toString());
            dateEdit->setDate(q.value(2).toDate());
            peselEdit->setText(q.value(3).toString());
            int i = sexCombo->findText(q.value(4).toString());
            if (i >= 0) sexCombo->setCurrentIndex(i);
        }
    }

    int editId;
    QLineEdit *surnameEdit;
    QLineEdit *nameEdit;
    QDateEdit *dateEdit;
    QLineEdit *peselEdit;
    QComboBox *sexCombo;
};

// dialog badania
class AddStudyDialog : public QDialog
{
    Q_OBJECT
public:
    // editId > 0 = edycja
    AddStudyDialog(int patientId, QWidget *parent = nullptr, int editId = -1)
        : QDialog(parent), patientId(patientId), editId(editId)
    {
        setWindowTitle(editId > 0 ? "Edytuj badanie" : "Dodaj badanie");

        dateEdit = new QDateEdit(this);
        dateEdit->setCalendarPopup(true);
        dateEdit->setDisplayFormat("yyyy-MM-dd");
        dateEdit->setDate(QDate::currentDate());

        typeCombo = new QComboBox(this);
        typeCombo->addItems({"RTG", "CT", "USG", "MRI"});

        descEdit = new QLineEdit(this);

        QFormLayout *form = new QFormLayout();
        form->addRow("Data:", dateEdit);
        form->addRow("Typ:", typeCombo);
        form->addRow("Opis:", descEdit);

        QDialogButtonBox *buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &AddStudyDialog::zapisz);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        QVBoxLayout *lay = new QVBoxLayout(this);
        lay->addLayout(form);
        lay->addWidget(buttons);

        if (editId > 0)
            wczytajDane();
    }

private slots:
    void zapisz()
    {
        QSqlQuery q;
        if (editId > 0) {
            q.prepare("UPDATE studies SET study_date=:date, type=:type, "
                      "description=:desc WHERE id=:id");
            q.bindValue(":id", editId);
        } else {
            q.prepare("INSERT INTO studies (patient_id, study_date, type, description) "
                      "VALUES (:pid, :date, :type, :desc)");
            q.bindValue(":pid", patientId);
        }
        q.bindValue(":date", dateEdit->date().toString("yyyy-MM-dd"));
        q.bindValue(":type", typeCombo->currentText());
        q.bindValue(":desc", descEdit->text());

        if (!q.exec()) {
            QMessageBox::critical(this, "Blad",
                "Nie udalo sie zapisac badania:\n" + q.lastError().text());
            return;
        }

        accept();
    }

private:
    void wczytajDane()
    {
        QSqlQuery q;
        q.prepare("SELECT study_date, type, description FROM studies WHERE id=:id");
        q.bindValue(":id", editId);
        q.exec();
        if (q.next()) {
            dateEdit->setDate(q.value(0).toDate());
            int i = typeCombo->findText(q.value(1).toString());
            if (i >= 0) typeCombo->setCurrentIndex(i);
            descEdit->setText(q.value(2).toString());
        }
    }

    int patientId;
    int editId;
    QDateEdit *dateEdit;
    QComboBox *typeCombo;
    QLineEdit *descEdit;
};

// glowne okno
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr)
        : QMainWindow(parent), currentPatientId(-1)
    {
        setWindowTitle("Baza medycznych badan obrazowych");
        resize(1000, 600);

        QToolBar *tb = addToolBar("Narzedzia");
        QAction *aPacjent = tb->addAction("Dodaj pacjenta");
        QAction *aBadanie = tb->addAction("Dodaj badanie");
        QAction *aDicom = tb->addAction("Importuj DICOM");
        QAction *aSeria = tb->addAction("Importuj serie");
        connect(aPacjent, &QAction::triggered, this, &MainWindow::dodajPacjenta);
        connect(aBadanie, &QAction::triggered, this, &MainWindow::dodajBadanie);
        connect(aDicom, &QAction::triggered, this, &MainWindow::importujDicom);
        connect(aSeria, &QAction::triggered, this, &MainWindow::importujSerie);

        // lewa strona - pacjenci + filtr
        QWidget *leftPanel = new QWidget(this);
        QVBoxLayout *leftLay = new QVBoxLayout(leftPanel);
        filterEdit = new QLineEdit(leftPanel);
        filterEdit->setPlaceholderText("Szukaj po nazwisku...");
        patientsView = new QTableView(leftPanel);
        patientsView->setSelectionBehavior(QAbstractItemView::SelectRows);
        patientsView->setEditTriggers(QAbstractItemView::NoEditTriggers);
        leftLay->addWidget(new QLabel("Pacjenci:"));
        leftLay->addWidget(filterEdit);
        leftLay->addWidget(patientsView);

        // prawa strona - zakladki
        rightTabs = new QTabWidget(this);
        studiesView = new QTableView();
        studiesView->setSelectionBehavior(QAbstractItemView::SelectRows);
        studiesView->setEditTriggers(QAbstractItemView::NoEditTriggers);
        viewer = new DicomViewer();
        rightTabs->addTab(studiesView, "Badania");
        rightTabs->addTab(viewer, "Podglad DICOM");

        QSplitter *splitter = new QSplitter(this);
        splitter->addWidget(leftPanel);
        splitter->addWidget(rightTabs);
        splitter->setStretchFactor(1, 1);
        setCentralWidget(splitter);

        patientsModel = new QSqlQueryModel(this);
        studiesModel = new QSqlQueryModel(this);
        patientsView->setModel(patientsModel);
        studiesView->setModel(studiesModel);

        connect(patientsView, &QTableView::clicked, this, &MainWindow::onPatientClicked);
        connect(studiesView, &QTableView::doubleClicked, this, &MainWindow::onStudyDoubleClicked);
        connect(filterEdit, &QLineEdit::textChanged, this, &MainWindow::filtrujPacjentow);

        // menu pod PPM (edycja / usuwanie)
        patientsView->setContextMenuPolicy(Qt::CustomContextMenu);
        studiesView->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(patientsView, &QWidget::customContextMenuRequested, this, &MainWindow::patientMenu);
        connect(studiesView, &QWidget::customContextMenuRequested, this, &MainWindow::studyMenu);

        zaladujPacjentow();
    }

private slots:
    void onPatientClicked(const QModelIndex &index)
    {
        int row = index.row();
        currentPatientId = patientsModel->data(patientsModel->index(row, 0)).toInt();
        zaladujBadania(currentPatientId);
    }

    void onStudyDoubleClicked(const QModelIndex &index)
    {
        int row = index.row();
        int studyId = studiesModel->data(studiesModel->index(row, 0)).toInt();

        // zbieramy wszystkie warstwy tego badania, posortowane wg sciezki
        QSqlQuery q;
        q.prepare("SELECT file_path FROM images "
                  "WHERE study_id = :sid ORDER BY file_path");
        q.bindValue(":sid", studyId);
        q.exec();

        QStringList paths;
        while (q.next())
            paths << q.value(0).toString();

        if (!paths.isEmpty()) {
            viewer->loadSeries(paths);
            rightTabs->setCurrentWidget(viewer);
        } else {
            QMessageBox::information(this, "Brak obrazu",
                "To badanie nie ma jeszcze zaimportowanych plikow DICOM.");
        }
    }

    void dodajPacjenta()
    {
        AddPatientDialog dlg(this);
        if (dlg.exec() == QDialog::Accepted) {
            zaladujPacjentow(filterEdit->text());
        }
    }

    void dodajBadanie()
    {
        if (currentPatientId < 0) {
            QMessageBox::warning(this, "Uwaga", "Najpierw wybierz pacjenta z listy.");
            return;
        }
        AddStudyDialog dlg(currentPatientId, this);
        if (dlg.exec() == QDialog::Accepted) {
            zaladujBadania(currentPatientId);
        }
    }

    void importujDicom()
    {
        // import pliku dicom do zaznaczonego badania
        int row = studiesView->currentIndex().row();
        if (row < 0) {
            QMessageBox::warning(this, "Uwaga",
                "Wybierz badanie, do ktorego chcesz dodac plik DICOM.");
            return;
        }
        int studyId = studiesModel->data(studiesModel->index(row, 0)).toInt();

        QString path = QFileDialog::getOpenFileName(this, "Wybierz plik DICOM",
            QString(), "Pliki DICOM (*.dcm);;Wszystkie pliki (*)");
        if (path.isEmpty())
            return;

        QSqlQuery q;
        q.prepare("INSERT INTO images (study_id, file_path) VALUES (:sid, :path)");
        q.bindValue(":sid", studyId);
        q.bindValue(":path", path);
        if (!q.exec()) {
            QMessageBox::critical(this, "Blad",
                "Nie udalo sie dodac obrazu:\n" + q.lastError().text());
            return;
        }

        QMessageBox::information(this, "OK", "Zaimportowano plik DICOM.");
        viewer->loadFile(path);
        rightTabs->setCurrentWidget(viewer);
    }

    void importujSerie()
    {
        // import calego folderu z seria (wiele plikow .dcm = warstwy)
        int row = studiesView->currentIndex().row();
        if (row < 0) {
            QMessageBox::warning(this, "Uwaga",
                "Wybierz badanie, do ktorego chcesz dodac serie.");
            return;
        }
        int studyId = studiesModel->data(studiesModel->index(row, 0)).toInt();

        QString dir = QFileDialog::getExistingDirectory(this, "Wybierz folder z seria DICOM");
        if (dir.isEmpty())
            return;

        QDir d(dir);
        QStringList pliki = d.entryList(QStringList() << "*.dcm", QDir::Files, QDir::Name);
        if (pliki.isEmpty()) {
            QMessageBox::warning(this, "Uwaga", "W tym folderze nie ma plikow .dcm");
            return;
        }

        // wszystkie pliki z folderu dodajemy jako warstwy tego badania
        QStringList fullPaths;
        for (const QString &p : pliki) {
            QString full = d.absoluteFilePath(p);
            fullPaths << full;
            QSqlQuery q2;
            q2.prepare("INSERT INTO images (study_id, file_path) VALUES (:sid, :path)");
            q2.bindValue(":sid", studyId);
            q2.bindValue(":path", full);
            q2.exec();
        }

        QMessageBox::information(this, "OK",
            QString("Zaimportowano serie: %1 warstw.").arg(pliki.size()));
        viewer->loadSeries(fullPaths);
        rightTabs->setCurrentWidget(viewer);
    }

    void filtrujPacjentow(const QString &text)
    {
        zaladujPacjentow(text);
    }

    void patientMenu(const QPoint &pos)
    {
        QModelIndex idx = patientsView->indexAt(pos);
        if (!idx.isValid())
            return;
        int pid = patientsModel->data(patientsModel->index(idx.row(), 0)).toInt();

        QMenu menu;
        QAction *aEdit = menu.addAction("Edytuj pacjenta");
        QAction *aDel = menu.addAction("Usun pacjenta");
        QAction *chosen = menu.exec(patientsView->viewport()->mapToGlobal(pos));

        if (chosen == aEdit) {
            AddPatientDialog dlg(this, pid);
            if (dlg.exec() == QDialog::Accepted)
                zaladujPacjentow(filterEdit->text());
        } else if (chosen == aDel) {
            if (QMessageBox::question(this, "Usuwanie",
                    "Usunac pacjenta razem z jego badaniami?") != QMessageBox::Yes)
                return;
            usunPacjenta(pid);
            zaladujPacjentow(filterEdit->text());
            studiesModel->clear();
            currentPatientId = -1;
        }
    }

    void studyMenu(const QPoint &pos)
    {
        QModelIndex idx = studiesView->indexAt(pos);
        if (!idx.isValid())
            return;
        int sid = studiesModel->data(studiesModel->index(idx.row(), 0)).toInt();

        QMenu menu;
        QAction *aEdit = menu.addAction("Edytuj badanie");
        QAction *aDel = menu.addAction("Usun badanie");
        QAction *chosen = menu.exec(studiesView->viewport()->mapToGlobal(pos));

        if (chosen == aEdit) {
            AddStudyDialog dlg(currentPatientId, this, sid);
            if (dlg.exec() == QDialog::Accepted)
                zaladujBadania(currentPatientId);
        } else if (chosen == aDel) {
            if (QMessageBox::question(this, "Usuwanie",
                    "Usunac to badanie razem z obrazami?") != QMessageBox::Yes)
                return;
            usunBadanie(sid);
            zaladujBadania(currentPatientId);
        }
    }

private:
    void usunPacjenta(int pid)
    {
        // najpierw dzieci (obrazy, badania), potem pacjent
        QSqlQuery q;
        q.exec(QString("DELETE FROM images WHERE study_id IN "
                       "(SELECT id FROM studies WHERE patient_id=%1)").arg(pid));
        q.exec(QString("DELETE FROM studies WHERE patient_id=%1").arg(pid));
        q.exec(QString("DELETE FROM patients WHERE id=%1").arg(pid));
    }

    void usunBadanie(int sid)
    {
        QSqlQuery q;
        q.exec(QString("DELETE FROM images WHERE study_id=%1").arg(sid));
        q.exec(QString("DELETE FROM studies WHERE id=%1").arg(sid));
    }

    void zaladujPacjentow(const QString &filtr = QString())
    {
        QString sql = "SELECT id, surname, name, birth_date, pesel, sex FROM patients";
        if (!filtr.isEmpty()) {
            sql += " WHERE surname ILIKE '%" + filtr + "%'";
        }
        sql += " ORDER BY surname";

        patientsModel->setQuery(sql);
        patientsModel->setHeaderData(1, Qt::Horizontal, "Nazwisko");
        patientsModel->setHeaderData(2, Qt::Horizontal, "Imie");
        patientsModel->setHeaderData(3, Qt::Horizontal, "Data ur.");
        patientsModel->setHeaderData(4, Qt::Horizontal, "PESEL");
        patientsModel->setHeaderData(5, Qt::Horizontal, "Plec");
        patientsView->hideColumn(0);
    }

    void zaladujBadania(int patientId)
    {
        QSqlQuery q;
        q.prepare("SELECT id, study_date, type, description FROM studies "
                  "WHERE patient_id = :pid ORDER BY study_date");
        q.bindValue(":pid", patientId);
        q.exec();

        studiesModel->setQuery(std::move(q));
        studiesModel->setHeaderData(1, Qt::Horizontal, "Data");
        studiesModel->setHeaderData(2, Qt::Horizontal, "Typ");
        studiesModel->setHeaderData(3, Qt::Horizontal, "Opis");
        studiesView->hideColumn(0);
    }

    QTableView *patientsView;
    QTableView *studiesView;
    QSqlQueryModel *patientsModel;
    QSqlQueryModel *studiesModel;
    QTabWidget *rightTabs;
    QLineEdit *filterEdit;
    DicomViewer *viewer;
    int currentPatientId;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // dekodery skompresowanych DICOM (JPEG, JPEG-LS)
    DJDecoderRegistration::registerCodecs();
    DJLSDecoderRegistration::registerCodecs();

    QSqlDatabase db = QSqlDatabase::addDatabase("QPSQL");
    db.setHostName("localhost");
    db.setPort(5433);
    db.setDatabaseName("medbaza");
    db.setUserName("postgres");
    db.setPassword("postgres");

    if (!db.open()) {
        QMessageBox::critical(nullptr, "Blad",
            "Nie udalo sie polaczyc z baza:\n" + db.lastError().text());
        return 1;
    }

    if (!initDB()) {
        QMessageBox::critical(nullptr, "Blad", "Nie udalo sie zainicjalizowac bazy");
        return 1;
    }

    MainWindow w;
    w.show();

    return app.exec();
}

#include "main.moc"
