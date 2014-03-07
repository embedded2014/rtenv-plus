#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <dirent.h>
#include <unistd.h>
#include <string.h>
#include <error.h>
#include <errno.h>

#define ERR_NO_DIR 1
#define ERR_NO_OUT 2
#define ERR_OPEN_OUT 3
#define ERR_OPEN_FILE 4

#define ERR(err) (ERR_##err)

#define PATH_LEN 31
#define BUF_SIZE 1024

struct entry {
    uint32_t parent;
    uint32_t prev;
    uint32_t next;
    uint32_t isdir;
    uint32_t len;
    uint8_t name[PATH_LEN + 1];
};

size_t fwrite_off(const void *ptr, size_t size, size_t nmemb, FILE *stream, off_t off)
{
    fseek(stream, off, SEEK_SET);
    return fwrite(ptr, size, nmemb, stream);
}

int procfile(const char *filename, char *fullpath, FILE *outfile)
{
    FILE *infile;
    char buf[BUF_SIZE];
    size_t size;

    infile = fopen(fullpath, "rb");
    if (!infile) {
        error(ERR(OPEN_FILE), errno, "Cannot open file '%s'\n", fullpath);
    }

    while ((size = fread(buf, 1, BUF_SIZE, infile))) {
        fwrite(buf, 1, size, outfile);
    }

    fclose(infile);

    return ftell(outfile);
}

int procdir(const char *dirname, char *fullpath, FILE *outfile)
{
    DIR *dirfile;
    struct dirent *direntry;

    strcat(fullpath, "/");
    size_t fullpath_len = strlen(fullpath);

    dirfile = opendir(fullpath);
    if (!dirfile) {
        error(ERR(OPEN_OUT), errno, "Cannot open directory '%s'\n", fullpath);
    }

    int parent_entry = ftell(outfile) - sizeof(struct entry);
    int prev_entry = 0; /* prev = 0 means no prev */
    int next_entry = ftell(outfile);
    int this_entry = next_entry;

    struct entry entry;
    entry.parent = parent_entry;
    entry.prev = 0;
    entry.next = 0;

    /* Scan entries */
    while ((direntry = readdir(dirfile))) {
        /* Ignore hidden files and itself */
        if (*direntry->d_name == '.')
            continue;

        this_entry = next_entry;

        entry.prev = prev_entry;
        strncpy((void*)entry.name, direntry->d_name, PATH_LEN);

        strcpy(fullpath + fullpath_len, direntry->d_name);

        /* Reservion for this entry */
        fwrite_off(&entry, sizeof(entry), 1, outfile, this_entry);

        /* Process entry */
        if (direntry->d_type == DT_DIR) {
            entry.isdir = 1;
            next_entry = procdir(direntry->d_name, fullpath, outfile);
        }
        else {
            entry.isdir = 0;
            next_entry = procfile(direntry->d_name, fullpath, outfile);
        }

        entry.next = next_entry;
        entry.len = next_entry - (this_entry + sizeof(entry));

        /* Write entry */
        fwrite_off(&entry, sizeof(entry), 1, outfile, this_entry);

        prev_entry = this_entry;
    }

    /* Clear next of last entry */
    if (entry.next != 0) {
        entry.next = 0;
        fwrite_off(&entry, sizeof(entry), 1, outfile, this_entry);
    }


    closedir(dirfile);

    return next_entry;
}

int main (int argc, char *argv[])
{
    int c;
    FILE *outfile;
    const char *outname = NULL;
    const char *dirname = NULL;
    char fullpath[PATH_MAX] = {0};

    while ((c = getopt(argc, argv, "o:d:")) != -1) {
        switch (c) {
            case 'd':
                if (optarg)
                    dirname = optarg;
                else
                    error(ERR(NO_DIR), EINVAL,
                          "-d option need a input directory.\n");
                break;
            case 'o':
                if (optarg)
                    outname = optarg;
                else
                    error(ERR(NO_OUT), EINVAL,
                          "-o option need a output file name.\n");
                break;
            default:
                ;
        }
    }

    if (!dirname) {
        dirname = ".";
    }

    if (outname)
        outfile = fopen(outname, "wb");
    else
        outfile = stdout;

    if (!outfile) {
        error(ERR(OPEN_OUT), errno,
              "Cannot open output file '%s'\n", outname);
    }

    strcpy(fullpath, dirname);

    /* Reservion for root entry */
    struct entry entry;
    fwrite_off(&entry, sizeof(entry), 1, outfile, 0);
    size_t end = procdir(dirname, fullpath, outfile);

    entry.parent = 0;
    entry.prev = 0;
    entry.next = 0;
    entry.isdir = 1;
    entry.len = end - sizeof(entry);
    fwrite_off(&entry, sizeof(entry), 1, outfile, 0);

    /* Clean up*/
    fclose(outfile);

    return 0;
}
