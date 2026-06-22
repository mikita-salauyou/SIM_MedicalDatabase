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
#include <QPushButton>
#include <QMessageBox>
#include <QMenu>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QStringList>
#include <QAbstractItemView>
#include <QModelIndex>
#include <QImage>
#include <QPixmap>
#include <QWheelEvent>
#include <QDate>
#include <QDebug>
#include <QFile>
#include <QXmlStreamWriter>
#include <QDateTime>
#include <cstring>
#include <utility>

#include <dcmtk/config/osconfig.h>
#include <dcmtk/dcmimgle/dcmimage.h>
#include <dcmtk/dcmjpeg/djdecode.h>
#include <dcmtk/dcmjpls/djdecode.h>
#include <dcmtk/dcmdata/dcfilefo.h>
#include <dcmtk/dcmdata/dcdeftag.h>

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

    // seria nalezy do badania, ma w sobie wiele obrazow
    q.exec("CREATE TABLE IF NOT EXISTS series ("
           "id SERIAL PRIMARY KEY,"
           "study_id INTEGER REFERENCES studies(id),"
           "modality VARCHAR(16),"
           "description TEXT)");

    // katalogi z plikami na dysku
    q.exec("CREATE TABLE IF NOT EXISTS sources ("
           "id SERIAL PRIMARY KEY,"
           "nazwa TEXT,"
           "sciezka TEXT UNIQUE)");

    // file_path jest wzgledny do katalogu zrodla
    q.exec("CREATE TABLE IF NOT EXISTS images ("
           "id SERIAL PRIMARY KEY,"
           "series_id INTEGER REFERENCES series(id),"
           "source_id INTEGER REFERENCES sources(id),"
           "file_path TEXT)");

    return true;
}

// wyciaga modality (CT/MR...) z pliku dicom
QString odczytajModality(const QString &path)
{
    DcmFileFormat ff;
    if (ff.loadFile(path.toLocal8Bit().constData()).good()) {
        OFString m;
        if (ff.getDataset()->findAndGetOFString(DCM_Modality, m).good())
            return QString::fromLatin1(m.c_str());
    }
    return "";
}

// szuka zrodla dla danego katalogu, a jak nie ma to dodaje nowe
int zrodloDlaKatalogu(const QString &absDir)
{
    QSqlQuery q;
    q.exec("SELECT id, sciezka FROM sources");
    while (q.next()) {
        QString s = q.value(1).toString();
        if (absDir == s || absDir.startsWith(s + "/"))
            return q.value(0).toInt();
    }
    QSqlQuery ins;
    ins.prepare("INSERT INTO sources (nazwa, sciezka) VALUES (?, ?) RETURNING id");
    ins.addBindValue(QDir(absDir).dirName());
    ins.addBindValue(absDir);
    if (ins.exec() && ins.next())
        return ins.value(0).toInt();
    return -1;
}

// zwraca sciezke katalogu zrodla po id
QString sciezkaZrodla(int id)
{
    QSqlQuery q;
    q.prepare("SELECT sciezka FROM sources WHERE id=?");
    q.addBindValue(id);
    q.exec();
    if (q.next())
        return q.value(0).toString();
    return "";
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
            q.prepare("UPDATE patients SET surname=?, name=?, "
                      "birth_date=?, pesel=?, sex=? WHERE id=?");
            q.addBindValue(surnameEdit->text());
            q.addBindValue(nameEdit->text());
            q.addBindValue(dateEdit->date().toString("yyyy-MM-dd"));
            q.addBindValue(peselEdit->text());
            q.addBindValue(sexCombo->currentText());
            q.addBindValue(editId);
        } else {
            q.prepare("INSERT INTO patients (surname, name, birth_date, pesel, sex) "
                      "VALUES (?, ?, ?, ?, ?)");
            q.addBindValue(surnameEdit->text());
            q.addBindValue(nameEdit->text());
            q.addBindValue(dateEdit->date().toString("yyyy-MM-dd"));
            q.addBindValue(peselEdit->text());
            q.addBindValue(sexCombo->currentText());
        }

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
        q.prepare("SELECT surname, name, birth_date, pesel, sex FROM patients WHERE id=?");
        q.addBindValue(editId);
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
            q.prepare("UPDATE studies SET study_date=?, type=?, "
                      "description=? WHERE id=?");
            q.addBindValue(dateEdit->date().toString("yyyy-MM-dd"));
            q.addBindValue(typeCombo->currentText());
            q.addBindValue(descEdit->text());
            q.addBindValue(editId);
        } else {
            q.prepare("INSERT INTO studies (patient_id, study_date, type, description) "
                      "VALUES (?, ?, ?, ?)");
            q.addBindValue(patientId);
            q.addBindValue(dateEdit->date().toString("yyyy-MM-dd"));
            q.addBindValue(typeCombo->currentText());
            q.addBindValue(descEdit->text());
        }

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
        q.prepare("SELECT study_date, type, description FROM studies WHERE id=?");
        q.addBindValue(editId);
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

// okno logowania do bazy
class LoginDialog : public QDialog
{
    Q_OBJECT
public:
    LoginDialog(QWidget *parent = nullptr) : QDialog(parent)
    {
        setWindowTitle("Logowanie do bazy danych");

        hostEdit = new QLineEdit("127.0.0.1", this);
        portEdit = new QLineEdit("5433", this);
        dbEdit = new QLineEdit("medbaza", this);
        userEdit = new QLineEdit("postgres", this);
        passEdit = new QLineEdit("postgres", this);
        passEdit->setEchoMode(QLineEdit::Password);

        QFormLayout *form = new QFormLayout();
        form->addRow("Serwer:", hostEdit);
        form->addRow("Port:", portEdit);
        form->addRow("Baza:", dbEdit);
        form->addRow("Uzytkownik:", userEdit);
        form->addRow("Haslo:", passEdit);

        QDialogButtonBox *buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        buttons->button(QDialogButtonBox::Ok)->setText("Polacz");
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

        QVBoxLayout *lay = new QVBoxLayout(this);
        lay->addLayout(form);
        lay->addWidget(buttons);
    }

    // tekst polaczenia dla sterownika PostgreSQL ODBC
    QString connectionString() const
    {
#ifdef Q_OS_WIN
        QString driver = "PostgreSQL Unicode(x64)";
#else
        QString driver = "PostgreSQL Unicode";
#endif
        return QString("Driver={%1};Server=%2;Port=%3;"
                       "Database=%4;Uid=%5;Pwd=%6;")
            .arg(driver, hostEdit->text(), portEdit->text(), dbEdit->text(),
                 userEdit->text(), passEdit->text());
    }

private:
    QLineEdit *hostEdit;
    QLineEdit *portEdit;
    QLineEdit *dbEdit;
    QLineEdit *userEdit;
    QLineEdit *passEdit;
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
        QAction *aEksport = tb->addAction("Eksportuj XML");
        connect(aPacjent, &QAction::triggered, this, &MainWindow::dodajPacjenta);
        connect(aBadanie, &QAction::triggered, this, &MainWindow::dodajBadanie);
        connect(aDicom, &QAction::triggered, this, &MainWindow::importujDicom);
        connect(aSeria, &QAction::triggered, this, &MainWindow::importujSerie);
        connect(aEksport, &QAction::triggered, this, &MainWindow::eksportujXml);

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

        // bierzemy wszystkie obrazy badania, sciezka = zrodlo + sciezka wzgledna
        QSqlQuery q;
        q.prepare("SELECT src.sciezka || '/' || i.file_path "
                  "FROM images i "
                  "JOIN series se ON i.series_id = se.id "
                  "JOIN sources src ON i.source_id = src.id "
                  "WHERE se.study_id = ? ORDER BY i.file_path");
        q.addBindValue(studyId);
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

        QFileInfo fi(path);
        int sourceId = zrodloDlaKatalogu(fi.absolutePath());
        QString rel = QDir(sciezkaZrodla(sourceId)).relativeFilePath(fi.absoluteFilePath());

        // pojedynczy plik = seria z jedna warstwa
        QSqlQuery qs;
        qs.prepare("INSERT INTO series (study_id, modality, description) "
                   "VALUES (?, ?, 'pojedynczy plik') RETURNING id");
        qs.addBindValue(studyId);
        qs.addBindValue(odczytajModality(path));
        if (!qs.exec() || !qs.next()) {
            QMessageBox::critical(this, "Blad",
                "Nie udalo sie dodac serii:\n" + qs.lastError().text());
            return;
        }
        int seriesId = qs.value(0).toInt();

        QSqlQuery q;
        q.prepare("INSERT INTO images (series_id, source_id, file_path) "
                  "VALUES (?, ?, ?)");
        q.addBindValue(seriesId);
        q.addBindValue(sourceId);
        q.addBindValue(rel);
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

        int sourceId = zrodloDlaKatalogu(d.absolutePath());
        QString sciezkaZr = sciezkaZrodla(sourceId);

        // jedna seria na caly folder
        QSqlQuery qs;
        qs.prepare("INSERT INTO series (study_id, modality, description) "
                   "VALUES (?, ?, ?) RETURNING id");
        qs.addBindValue(studyId);
        qs.addBindValue(odczytajModality(d.absoluteFilePath(pliki.first())));
        qs.addBindValue(d.dirName());
        if (!qs.exec() || !qs.next()) {
            QMessageBox::critical(this, "Blad",
                "Nie udalo sie dodac serii:\n" + qs.lastError().text());
            return;
        }
        int seriesId = qs.value(0).toInt();

        QStringList fullPaths;
        for (const QString &p : pliki) {
            QString full = d.absoluteFilePath(p);
            fullPaths << full;
            QString rel = QDir(sciezkaZr).relativeFilePath(full);
            QSqlQuery q2;
            q2.prepare("INSERT INTO images (series_id, source_id, file_path) "
                       "VALUES (?, ?, ?)");
            q2.addBindValue(seriesId);
            q2.addBindValue(sourceId);
            q2.addBindValue(rel);
            q2.exec();
        }

        QMessageBox::information(this, "OK",
            QString("Zaimportowano serie: %1 warstw.").arg(pliki.size()));
        viewer->loadSeries(fullPaths);
        rightTabs->setCurrentWidget(viewer);
    }

    void eksportujXml()
    {
        // zrzut wszystkich obrazow do pliku XML (sciezka, rozmiar, data)
        QString plik = QFileDialog::getSaveFileName(this, "Zapisz XML",
            "obrazy.xml", "Pliki XML (*.xml)");
        if (plik.isEmpty())
            return;

        QFile f(plik);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QMessageBox::critical(this, "Blad", "Nie udalo sie otworzyc pliku do zapisu.");
            return;
        }

        QSqlQuery q;
        q.exec("SELECT src.sciezka || '/' || i.file_path, p.surname, p.name, "
               "st.study_date, st.type, se.modality "
               "FROM images i "
               "JOIN series se ON i.series_id = se.id "
               "JOIN studies st ON se.study_id = st.id "
               "JOIN patients p ON st.patient_id = p.id "
               "JOIN sources src ON i.source_id = src.id "
               "ORDER BY i.id");

        QXmlStreamWriter xml(&f);
        xml.setAutoFormatting(true);
        xml.writeStartDocument();
        xml.writeStartElement("obrazy");

        int licznik = 0;
        while (q.next()) {
            QString sciezka = q.value(0).toString();
            QFileInfo fi(sciezka);

            xml.writeStartElement("obraz");
            xml.writeTextElement("pacjent", q.value(1).toString() + " " + q.value(2).toString());
            xml.writeTextElement("badanie", q.value(3).toString() + " " + q.value(4).toString());
            xml.writeTextElement("modalnosc", q.value(5).toString());
            xml.writeTextElement("sciezka", sciezka);
            xml.writeTextElement("rozmiar", QString::number(fi.size()));
            xml.writeTextElement("data", fi.lastModified().toString("yyyy-MM-dd HH:mm"));
            xml.writeEndElement();
            licznik++;
        }

        xml.writeEndElement();
        xml.writeEndDocument();
        f.close();

        QMessageBox::information(this, "OK",
            QString("Zapisano %1 obrazow do pliku XML.").arg(licznik));
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
        // najpierw dzieci (obrazy, serie, badania), potem pacjent
        QSqlQuery q;
        q.exec(QString("DELETE FROM images WHERE series_id IN "
                       "(SELECT se.id FROM series se JOIN studies st ON se.study_id=st.id "
                       "WHERE st.patient_id=%1)").arg(pid));
        q.exec(QString("DELETE FROM series WHERE study_id IN "
                       "(SELECT id FROM studies WHERE patient_id=%1)").arg(pid));
        q.exec(QString("DELETE FROM studies WHERE patient_id=%1").arg(pid));
        q.exec(QString("DELETE FROM patients WHERE id=%1").arg(pid));
    }

    void usunBadanie(int sid)
    {
        QSqlQuery q;
        q.exec(QString("DELETE FROM images WHERE series_id IN "
                       "(SELECT id FROM series WHERE study_id=%1)").arg(sid));
        q.exec(QString("DELETE FROM series WHERE study_id=%1").arg(sid));
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
                  "WHERE patient_id = ? ORDER BY study_date");
        q.addBindValue(patientId);
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

    // logowanie do bazy przez ODBC - powtarzaj az do udanego polaczenia
    LoginDialog login;
    while (true) {
        if (login.exec() != QDialog::Accepted)
            return 0;
        if (QSqlDatabase::contains(QSqlDatabase::defaultConnection))
            QSqlDatabase::removeDatabase(QSqlDatabase::defaultConnection);
        QSqlDatabase db = QSqlDatabase::addDatabase("QODBC");
        db.setDatabaseName(login.connectionString());
        if (db.open())
            break;
        QMessageBox::critical(nullptr, "Blad logowania",
            "Nie udalo sie polaczyc z baza:\n" + db.lastError().text());
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
