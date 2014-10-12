/* Memo is a Unix-style note taking software.
 *
 * Copyright (C) 2014 Niko Rosvall <niko@newsworm.net>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * memo.c implements a flexible, Unix-style note taking software.
 *
 * If you're interested hacking Memo, please remember:
 * coding style is pretty much the same as for Linux kernel.
 * see https://www.kernel.org/doc/Documentation/CodingStyle
 *
 * When adding features, please consider if they can be
 * accomplished in a sane way with standard unix tools instead.
 *
 * If you port Memo for another platform, please let me know,
 * no reason for that other than it's nice to know where Memo runs.
 * Memo should be fairly portable for POSIX systems. I don't know
 * about Windows...uninstd.h is not natively available for it(?).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <stdarg.h>
#include <fcntl.h>

/* Function declarations */
char *read_memo_line(FILE *fp);
char *get_memo_file_path();
char *get_temp_memo_path();
int   add_note(const char *content);
int   get_next_id();
int   delete_note(int id);
int   show_notes();
int   count_notes(FILE *fp);
int   search_notes(const char *search);
const char *export_html(const char *path);
void  output_line(char *line);
void  show_latest(int count);
FILE *get_memo_file_ptr();
void  usage();
void  fail(FILE *out, const char *fmt, ...);
int   delete_all();

#define VERSION 0.6


/* Count the lines of the file as a note is always one liner,
 * lines == note count.
 *
 * File pointer is rewinded to the beginning of the file.
 *
 * Returns the count or -1 if the file pointer is null.
 * Caller must close fp after calling the function successfully.
 */
int count_notes(FILE *fp)
{
	int count = 0;
	int ch = 0;

	if (!fp) {
		fail(stderr,"%s: NULL file pointer\n", __func__);
		return -1;
	}

        /* Count lines by new line characters */
	while (!feof(fp)) {
		ch = fgetc(fp);

		if (ch == '\n')
			count++;
	}

        /* Go to beginning of the file */
	rewind(fp);

        /* return the count, ignoring the last empty line */
        /* "empty line" is because of the sick way I'm using feof. */
	if (count == 0)
		return count;
	else
		return count - 1;
}


/* A simple error reporting function */
void fail(FILE *out, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(out, fmt, ap);
	va_end(ap);
}


/* Get open FILE* for ~./memo file.
 * Returns NULL of failure.
 * Caller must close the file pointer after calling the function
 * succesfully.
 */
FILE *get_memo_file_ptr(char *mode)
{
	FILE *fp = NULL;
	char *path = get_memo_file_path();

	if (path == NULL) {
		fail(stderr,"%s: error getting ~./memo path\n",
			__func__);
		return NULL;
	}

	fp = fopen(path, mode);

	if (fp == NULL) {
		fail(stderr,"%s: error opening %s\n", __func__, path);
		return NULL;
	}

	free(path);

	return fp;
}

/* Reads a line from source pointed by FILE*.
 * Return NULL on failure.
 * Caller is responsible for freeing the return value
 */
char *read_memo_line(FILE *fp)
{
	if (!fp)
		return NULL;

	int length = 128;
	char *buffer = NULL;

	buffer = (char*)malloc(sizeof(char) * length);

	if (buffer == NULL) {
		fail(stderr,"%s: malloc failed\n", __func__);
		return NULL;
	}

	int count = 0;

	char ch = getc(fp);

       /* Read char by char until the end of the line.
        * and allocate memory as needed.
        */
	while ((ch != '\n') && (ch != EOF)) {
		if (count == length) {
			length += 128;
			buffer = realloc(buffer, length);

			if (buffer == NULL) {
				fail(stderr,
					"%s: realloc failed\n",
					__func__);
				return NULL;
			}
		}

		buffer[count] = ch;
		count++;
		ch = getc(fp);
	}

	buffer[count] = '\0';
	buffer = realloc(buffer, count + 1);

	return buffer;
}


/* Simply read all the lines from the ~/.memo file
 * and return the id of the last line plus one.
 * If the file is missing or is empty, return 0
 * On error, returns -1
 */
int get_next_id()
{
	int id = 0;
	FILE *fp = NULL;
	char *line = NULL;
	int lines = 0;
	int current = 0;

	fp = get_memo_file_ptr("r");

	lines = count_notes(fp);

	if (lines == -1) {
		fail(stderr,"%s: counting lines failed\n", __func__);
		return -1;
	}

	while (1) {
		line = read_memo_line(fp);

		/* Check if we're at the last line */
		if (line && current == lines) {
			char *endptr;
			id = strtol(line, &endptr, 10);
			free(line);
			break;
	        }

		current++;

		if (line)
			free(line);
	}

	fclose(fp);

	return id + 1;
}


/* Show all notes.
 * Returns the number of notes;
 * Returns -1 on failure
 */
int show_notes()
{
	FILE *fp = NULL;
	char *line;
	int count = 0;
	int lines = 0;

	fp = get_memo_file_ptr("r");

	lines = count_notes(fp);
	count = lines;

	if (lines == -1) {
		fail(stderr,"%s: counting lines failed\n", __func__);
		return -1;
	}

	while (lines >= 0) {
		line = read_memo_line(fp);

		if (line) {
			output_line(line);
			free(line);
		}

		lines--;
	}

	fclose(fp);

	return count;
}


/* Search if a note contains the search term.
 * Returns the count of found notes or -1 if function fails.
 */
int search_notes(const char *search)
{
	FILE *fp = NULL;
	int count = 0;
	char *line;
	int lines = 0;

	fp = get_memo_file_ptr("r");

	lines = count_notes(fp);

	if (lines == -1) {
		fail(stderr,"%s: counting lines failed\n", __func__);
		return -1;
	}

	while (lines >= 0) {
		line = read_memo_line(fp);

		if (line){
			/* Check if the search term matches */
			const char *tmp = line;

			if ((strstr(tmp, search)) != NULL){
				output_line(line);
				count++;
			}

			free(line);
		}

		lines--;
	}

	fclose(fp);

	return count;
}


/* This functions handles the output of one line.
 * Just a simple wrapper for printf for now.
 * Preserved for the future use.
 */
void output_line(char *line)
{
	printf("%s\n", line);
}


/* Export current ~/.memo file to a html file
 * Return the path of the html file, or NULL on failure.
 */
const char *export_html(const char *path)
{
	FILE *fp = NULL;
	FILE *fpm = NULL;
	char *line = NULL;
	int lines = 0;

	fp = fopen(path, "w");

	if (!fp) {
		fail(stderr, "%s: failed to open %s\n", __func__, path);
		return NULL;
	}

	fpm = get_memo_file_ptr("r");
	lines = count_notes(fpm);

	if (lines == -1) {
		fail(stderr, "%s: counting lines failed\n", __func__);
		return NULL;
	}

	fprintf(fp,"<!DOCTYPE html>\n");
	fprintf(fp, "<html>\n<head>\n");
	fprintf(fp, "<meta charset=\"UTF-8\">\n");
	fprintf(fp, "<title>Memo notes</title>\n");
	fprintf(fp, "<style>pre{font-family: sans-serif;}</style>\n");
	fprintf(fp, "</head>\n<body>\n");
	fprintf(fp, "<h1>Notes from Memo</h1>\n");
	fprintf(fp, "<table>\n");

	while (lines >= 0) {
		line = read_memo_line(fpm);

		if (line) {
			fprintf(fp, "<tr><td><pre>%s</pre></td></tr>\n",
				line);
			free(line);
		}

		lines--;
	}

	fprintf(fp, "</table>\n</body>\n</html>\n");
	fclose(fp);
	fclose(fpm);

	return path;
}


/* Show latest n notes */
void show_latest(int n)
{
	FILE *fp = NULL;
	char *line;
	int lines = 0;
	int start;
	int current = 0;

	fp = get_memo_file_ptr("r");

	lines = count_notes(fp);

	if (lines != -1) {
		/* If n is bigger than the count of lines or smaller
		 * than zero we will show all the lines.
		 */
		if (n > lines || n < 0)
			start = -1;
		else
			start = lines - n;

		while (lines >= 0) {
			line = read_memo_line(fp);

			if (line) {
				if (current > start)
					output_line(line);
				free(line);
			}

			lines--;
			current++;
		}

		fclose(fp);
	} else
		fail(stderr,"%s: counting lines failed\n", __func__);
}


/* Deletes all notes. Function actually
 * simply removes ~/.memo file.
 * Returns 0 on success, -1 on failure.
 */
int delete_all()
{
	char *path = get_memo_file_path();

	if (path == NULL) {
		fail(stderr,"%s error getting .memo file path\n", __func__);
		return -1;
	}

	if (remove(path) != 0) {
		fail(stderr,"%s error removing %s\n", __func__, path);
	}

	free(path);

	return 0;
}


/* Delete a note by id.
 * Returns 0 on success and -1 on failure.
 */
int delete_note(int id)
{
	FILE *fp = NULL;
	FILE *tmpfp = NULL;
	char *line = NULL;
	char *tmp;
	int lines = 0;

	tmp = get_temp_memo_path();

	if (tmp == NULL) {
		fail(stderr,"%s: error getting a temp file\n",
			__func__);
		return -1;
	}

	char *memofile = get_memo_file_path();

	if (memofile == NULL) {
		fail(stderr,"%s: failed to get ~/.memo file path\n",
			__func__);
		return -1;
	}

	tmpfp = fopen(tmp, "w");

	if (tmpfp == NULL) {
		fail(stderr,"%s: error opening %s\n", __func__, tmp);
		free(memofile);
		return -1;
	}

	fp = get_memo_file_ptr("r");
	lines = count_notes(fp);

	if (lines == -1) {
		free(memofile);
		fclose(tmpfp);
		fail(stderr,"%s: counting lines failed\n", __func__);
		return -1;
	}

	while (lines >= 0) {
		line = read_memo_line(fp);

		if (line) {
			char *endptr;
			int curr = strtol(line, &endptr, 10);

			if (curr != id)
				fprintf(tmpfp, "%s\n", line);

			free(line);
		}

		lines--;
	}

	fclose(fp);
	fclose(tmpfp);

	if (access(memofile, F_OK) == 0)
		remove(memofile);

	rename(tmp, memofile);
	remove(tmp);

	free(memofile);
	free(tmp);

	return 0;
}


/*
 * Returns the path to $HOME/.memo file.
 * Caller is responsible for freeing the return value.
 */
char *get_memo_file_path()
{
	char *env = getenv("HOME");
	char *path = (char*)malloc(1024 * sizeof(char));

	if (path == NULL) {
		fail(stderr,"%s: malloc failed\n", __func__);
		return NULL;
	}

	if (env == NULL){
		fail(stderr,"%s: getenv(\"HOME\") failed\n", __func__);
		return NULL;
	}

	strcpy(path, env);
	strcat(path, "/.memo");

	return path;
}


/* Returns temporary .memo.tmp file.
 * It will be in the same directory
 * as the original .memo file.
 *
 * Returns NULL on failure.
 */
char *get_temp_memo_path()
{
	char *orig = get_memo_file_path();

	if (orig == NULL)
		return NULL;

	char *tmp = (char*)malloc(sizeof(char) * (strlen(orig) + 5));

	if (tmp == NULL) {
		free(orig);
		fail(stderr,"%s: malloc failed\n", __func__);
		return NULL;
	}

	strcpy(tmp, orig);
	strcat(tmp, ".tmp");

	free(orig);

	return tmp;
}


/*
 * .memo file format is following:
 *
 * id     date           content
 * |      |              |
 * |- id  |- yyy-MM-dd   |- actual note
 *
 * sections are separated by a tab character
 */
int add_note(const char *content)
{
	FILE *fp = NULL;
	time_t t;
	struct tm *ti;
	int id = -1;
	char date[11];


	fp = get_memo_file_ptr("a");

	if (fp == NULL) {
		fail(stderr,"%s: Error opening ~/.memo\n", __func__);
		return -1;
	}

	time(&t);
	ti = localtime(&t);

	id = get_next_id();

	if (id == -1) {
		id = 1;
	}

	strftime(date, 80, "%Y-%m-%d", ti);
	fprintf(fp, "%d\t%s\t%s\n", id, date, content);


	fclose(fp);

	return id;
}


void usage()
{
#define HELP "\
SYNOPSIS\n\
\n\
    memo [options]\n\
\n\
OPTIONS\n\
\n\
    -a <content>    Add a new note\n\
    -d <id>         Delete note by id\n\
    -D              Delete all notes\n\
    -e <path>       Export notes as html to a file\n\
    -f <search>     Find note by search term\n\
    -l <n>          Show latest n notes\n\
    -s              Show all notes\n\
\n\
    -h              Show short help and exit. This page\n\
    -V              Show version number of program\n\
\n\
DESCRIPTION\n\
\n\
    Memo is a note taking software for POSIX compatible operating systems.\n\
    The short notes are saved to user's home directory in ~/.memo file.\n\
\n\
EXAMPLES\n\
\n\
    Add a new note:\n\
        memo -a \"Remember to buy milk!\"\n\
\n\
    Search memos by string:\n\
        memo -f buy\n\
\n\
        Output:\n\
        4\t2014-10-10\tRemember to buy milk\n\
\n\
    Replace record 4 with new text:\n\
        memo -d 4 -a \"Remember to buy cheese\"\n\
\n\
    Add note from stdin:\n\
        echo \"My new note\" | memo\n\
\n\
AUTHORS\n\
    Copyright (C) 2014 Niko Rosvall <niko@newsworm.net>\n\
\n\
    Released under license GPL-3+. For more information, see\n\
    http://www.gnu.org/licenses\n\
"

	printf(HELP);
}


/* Program entry point */
int main(int argc, char *argv[])
{
	char *path = NULL;
	int c;
	char *stdinline = NULL;
	int has_valid_options = 0;

	path = get_memo_file_path();

	if (path == NULL)
		return -1;

	if (access(path,F_OK) != 0) {
		int fd = open(path, O_RDWR | O_CREAT,
			S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

		free(path);

		if (fd == -1) {
			fail(stderr,"%s: failed to create empty memo\n",
				__func__);
			return -1;
		}

		close(fd);
	}

	opterr = 0;

	if (argc == 1) {
	       /* No options available, so get data from stdin.
		* Assumes that the data is content for a new note.
		*/
		stdinline = read_memo_line(stdin);

		if (stdinline) {
			add_note(stdinline);
			free(stdinline);
		}
	}

	while ((c = getopt(argc, argv, "a:d:De:f:hl:sV")) != -1){
		has_valid_options = 1;

		switch(c){
		case 'a':
			add_note(optarg);
			break;
		case 'd':
			delete_note(atoi(optarg));
			break;
		case 'D':
			delete_all();
			break;
		case 'e':
			export_html(optarg);
			break;
		case 'f':
			search_notes(optarg);
			break;
		case 'h':
			usage();
			break;
		case 'l':
			show_latest(atoi(optarg));
			break;
		case 's':
			show_notes();
			break;
		case 'V':
			printf("Memo version %.1f\n", VERSION);
			break;
		case '?':
			if (optopt == 'a')
				printf("-a missing an argument <content>\n");
			else if (optopt == 'd')
				printf("-d missing an argument <id>\n");
			else if (optopt == 'e')
				printf("-e missing an argument <path>\n");
			else if (optopt == 'f')
				printf("-f missing an argument <search>\n");
			else if (optopt == 'l')
				printf("-l missing an argument <id>\n");
			else
				printf("invalid option, see memo -h for help\n");
			break;
		}
	}

	if (argc > 1 && !has_valid_options)
		printf("invalid input, see memo -h for help\n");

	return 0;
}
