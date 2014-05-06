#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <list>
#include <unistd.h>
#include <dirent.h>
#include <cerrno>
#include <StormLib.h>

using namespace std;

typedef struct _FileEntry {
    string  filename;
    string  fullPath;
    DWORD   fileSize;
    DWORD   compressedSize;
    //DWORD   lowDateTime;
    //DWORD   highDateTime;
    //LCID    locale;
    bool    isDir;
} FileEntry;

enum MODE {
    EXTRACT,
    LIST,
    ADD,
    DELETE,
};

string filename;
HANDLE archive;
vector<FileEntry> files;
vector<FileEntry> directories;

DWORD maxCompressedSize = 0;
DWORD maxFileSize = 0;

void printError(string message)
{
    int nError = GetLastError();

    message = "mpq: " + message;

    switch(nError) {
        case ERROR_BAD_FORMAT:
            cerr << message << ": Bad format" << endl;
            break;
        case ERROR_FILE_CORRUPT:
            cerr << message << ": File corrupted" << endl;
            break;
        default:
            perror(message.c_str());
            break;
    }
}

int intLenght(DWORD num)
{
    int i = 0;
    do {
        ++i;
        num /= 10;
    } while (num);
    return i;
}

bool listFiles(string pattern)
{
    SFILE_FIND_DATA findData;
    list<string> directoryNames;
    HANDLE handle = SFileFindFirstFile(archive, pattern.c_str(), &findData, 0);

    if (handle) {
        do {
            FileEntry e;

            e.filename          = findData.szPlainName;
            e.fullPath          = findData.cFileName;
            e.compressedSize    = findData.dwCompSize;
            e.fileSize          = findData.dwFileSize;
            //e.highDateTime      = findData.dwFileTimeHi;
            //e.lowDateTime       = findData.dwFileTimeLo;
            //e.locale            = findData.lcLocale;
            e.isDir = false;

            //TODO: check the platform
            replace(e.fullPath.begin(), e.fullPath.end(), '\\', '/');

            if (e.compressedSize > maxCompressedSize) {
                maxCompressedSize = e.compressedSize;
            }
            if (e.fileSize > maxFileSize) {
                maxFileSize = e.fileSize;
            }

            files.push_back(e);

            //Adding directories
            string directoriesChain;

            for (size_t offset = e.fullPath.find('/'), start = 0; offset != string::npos; start = offset + 1, offset = e.fullPath.find('/', start)) {
                directoriesChain += e.fullPath.substr(start, offset - start) + "/";
                directoryNames.push_back(directoriesChain);
            }

        } while (SFileFindNextFile(handle, &findData));

        SFileFindClose(handle);
    } else {
        printError("error finding pattern " + pattern);
        return false;
    }

    maxCompressedSize = intLenght(maxCompressedSize);
    maxFileSize = intLenght(maxFileSize);

    directoryNames.sort();
    directoryNames.unique();

    list<string>::iterator iter, iterEnd;

    for (iter = directoryNames.begin(), iterEnd = directoryNames.end(); iter != iterEnd; ++iter) {
        string fullPath = *iter;
        string tmpDirectoryName = fullPath.substr(0, fullPath.length() - 1);
        size_t offset = tmpDirectoryName.rfind('/');

        FileEntry e;

        e.filename = (offset != string::npos) ? tmpDirectoryName.substr(offset + 1) : tmpDirectoryName;
        e.fullPath = fullPath;
        e.compressedSize = 0;
        e.fileSize = 0;
        e.isDir = true;

        directories.push_back(e);
    }

    return true;
}

void printList()
{
    vector<FileEntry> filesAndDirectories = files;
    filesAndDirectories.insert(filesAndDirectories.end(), directories.begin(), directories.end());

    vector<FileEntry>::iterator iter, iterEnd;
    for (iter = filesAndDirectories.begin(), iterEnd = filesAndDirectories.end(); iter != iterEnd; ++iter) {
        cout.width(maxFileSize);
        cout << iter->fileSize << " ";
        cout.width(maxCompressedSize);
        cout << iter->compressedSize << " ";
        cout << iter->fullPath << endl;
    }
}

bool extractFile(vector<FileEntry>::iterator entry, string destinationDirectory)
{
    string destinationFile = destinationDirectory;
    destinationFile += entry->fullPath;

    size_t offset;
    offset = destinationFile.find_last_of("/");
    if (offset != string::npos) {
        string dest = destinationFile.substr(0, offset + 1);

        size_t start = dest.find("/", 0);
        while (start != string::npos) {
            string dirname = dest.substr(0, start);

            DIR* d = opendir(dirname.c_str());
            if (!d)
                mkdir(dirname.c_str(), S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
            else
                closedir(d);

            start = dest.find("/", start + 1);
        }
    }

    return SFileExtractFile(archive, entry->fullPath.c_str(), destinationFile.c_str(), 0);
}

void extractFiles(string destinationDirectory)
{
    if (!files.empty()) {
        cout << endl;
        cout << "Extracting files..." << endl;
        cout << endl;

        if (destinationDirectory.at(destinationDirectory.length() - 1) != '/')
            destinationDirectory += "/";

        vector<FileEntry>::iterator iter, iterEnd;
        for (iter = files.begin(), iterEnd = files.end(); iter != iterEnd; ++iter) {
            if (extractFile(iter, destinationDirectory))
                cout << iter->fullPath << endl;
            else
                printError("failed to extract the file '" + iter->fullPath + "'");
        }
    }
}


int main(int argc, char** argv)
{
    MODE mode = EXTRACT;
    string pattern = "*";
    string destinationDirectory = ".";

    int option;
    bool listResult = false;

    while ((option = getopt(argc, argv, "d:hZ")) != -1)
        switch (option) {
        case 'd':
            destinationDirectory = optarg;
            break;
        case 'Z':
            mode = LIST;
            break;
        case 'h':
        default:
            cout << "TODO: Add help page" << endl;
            exit(EXIT_SUCCESS);
        }

    filename = argv[optind];

    if (!SFileOpenArchive(filename.c_str(), 0, MPQ_OPEN_READ_ONLY, &archive)) {
        printError("error opening an archive");
        exit(EXIT_FAILURE);
    }

    cout << "Archive: " << filename << endl;

    switch (mode) {
    case LIST:
        if (argc > optind + 1)
            pattern = argv[optind + 1];
        if (listFiles(pattern))
            printList();
        break;
    case EXTRACT:
        if (argc > optind + 1)
            for (int i = optind + 1; i < argc; ++i)
                listResult = listFiles(string(argv[i]));
        else
            listResult = listFiles(pattern);

        if (listResult)
            extractFiles(destinationDirectory);
        break;
    default:
        break;
    }

    if (!SFileCloseArchive(archive)) {
        printError("error closing an archive");
        exit(EXIT_FAILURE);
    }

    exit(EXIT_SUCCESS);
}
// kate: indent-mode cstyle; indent-width 4; replace-tabs on; 
