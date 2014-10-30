/* Memo is a Unix-style note-taking software.
 *
 * Copyright (C) 2014 Niko Rosvall <niko@ideabyte.net>
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
 * memo.c implements a flexible, Unix-style note-taking software.
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

/* To enable _POSIX_C_SOURCE feature test macro */
#define _XOPEN_SOURCE 600

/* Make only POSIX.2 regexp functions available */
#define _POSIX_C_SOURCE 200112L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/types.h>
#include <regex.h>
#include <sys/stat.h>


typedef enum {
	DONE = 1,
	UNDONE = 2,
	DELETE = 3,
	DELETE_DONE = 4,
	STATUS_ERROR = 5,
	ALL_DONE = 6,
	POSTPONED = 7
} NoteStatus_t;


/* Function declarations */
static char *read_file_line(FILE *fp);
static char *get_memo_file_path();
static char *get_memo_default_path();
static char *get_memo_conf_path();
static char *get_temp_memo_path();
static char *get_memo_conf_value(const char *prop);
static int   is_valid_date_format(const char *date);
static int   add_note(const char *content, const char *date);
static int   get_next_id();
static int   delete_note(int id);
static int   show_notes(NoteStatus_t status);
static int   count_file_lines(FILE *fp);
static int   search_notes(const char *search);
static int   search_regexp(const char *regexp);
static const char *export_html(const char *path);
static void  output_default(char *line);
static void  output_postponed(char *line);
static void  show_latest(int count);
static FILE *get_memo_file_ptr();
static void  usage();
static void  fail(FILE *out, const char *fmt, ...);
static int   delete_all();
static void  show_memo_file_path();
static NoteStatus_t get_note_status(const char *line);
static int   mark_note_status(NoteStatus_t status, int id);
static void  note_status_replace(char *line, char new, char old);
static void  mark_as_done(FILE *fp, char *line);
static void  mark_as_undone(FILE *fp, char *line);
static void  mark_as_postponed(FILE *fp, char *line);


#define VERSION 1.2


/* Check if given date is in valid date format.
 * Memo assumes the date format to be yyyy-MM-dd.
 *
 * Functions returns 0 on success and -1 on failure.
 */
static int is_valid_date_format(const char *date)
{
	int d;
	int m;
	int y;
	int ret;

	/* contains number of days in each month from jan to dec */
	int day_count[12] = { 31, 28, 31, 30, 31, 30,
			      31, 31, 30, 31, 30, 31 };

	ret = sscanf(date, "%04d-%02d-%02d", &y, &m, &d);

	if (ret != 3) {
		fail(stderr,"%s: invalid date format\n", __func__);
		return -1;
	}

	/* Leap year check */
	if (y % 400 == 0 || y % 100 != 0 || y % 4 == 0)
		day_count[1] = 29;

	if (m < 13 && m > 0) {
		if (d <= day_count[m - 1])
			return 0;
		else
			fail(stderr, "%s: invalid day\n", __func__);
	} else {
		fail(stderr, "%s: invalid month\n", __func__);
	}

	fail(stderr, "%s: parsing date failed\n", __func__);

	return -1;
}


/* This function is used to count lines in .memo and ~/.memorc
 * files.
 *
 * Count the lines of the file as a note is always one liner,
 * lines == note count.
 *
 * File pointer is rewinded to the beginning of the file.
 *
 * Returns the count or -1 if the file pointer is null.
 * Caller must close fp after calling the function successfully.
 */
static int count_file_lines(FILE *fp)
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
static void fail(FILE *out, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(out, fmt, ap);
	va_end(ap);
}


/* Get open FILE* for .memo file.
 * Returns NULL of failure.
 * Caller must close the file pointer after calling the function
 * succesfully.
 */
static FILE *get_memo_file_ptr(char *mode)
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
 *
 * This function is used to read .memo as well as ~/.memorc
 * files line by line.
 *
 * Return NULL on failure.
 * Caller is responsible for freeing the return value
 */
static char *read_file_line(FILE *fp)
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


/* Simply read all the lines from the .memo file
 * and return the id of the last line plus one.
 * If the file is missing or is empty, return 0
 * On error, returns -1
 */
static int get_next_id()
{
	int id = 0;
	FILE *fp = NULL;
	char *line = NULL;
	int lines = 0;
	int current = 0;

	fp = get_memo_file_ptr("r");

	lines = count_file_lines(fp);

	if (lines == -1) {
		fail(stderr,"%s: counting lines failed\n", __func__);
		return -1;
	}

	while (1) {
		line = read_file_line(fp);

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


/* Show all notes. with status POSTPONED, postponed
 * notes are shown. Otherwise status is ignored and
 * all notes are displayed.
 *
 * Returns the number of notes. Returns -1 on failure
 */
static int show_notes(NoteStatus_t status)
{
	FILE *fp = NULL;
	char *line;
	int count = 0;
	int lines = 0;

	fp = get_memo_file_ptr("r");

	lines = count_file_lines(fp);
	count = lines;

	if (lines == -1) {
		fail(stderr,"%s: counting lines failed\n", __func__);
		return -1;
	}

	while (lines >= 0) {
		line = read_file_line(fp);

		if (line) {
			if (status == POSTPONED)
				output_postponed(line);
			else
				output_default(line);
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
static int search_notes(const char *search)
{
	FILE *fp = NULL;
	int count = 0;
	char *line;
	int lines = 0;

	fp = get_memo_file_ptr("r");

	lines = count_file_lines(fp);

	if (lines == -1) {
		fail(stderr,"%s: counting lines failed\n", __func__);
		return -1;
	}

	while (lines >= 0) {
		line = read_file_line(fp);

		if (line) {
			/* Check if the search term matches */
			const char *tmp = line;

			if ((strstr(tmp, search)) != NULL){
				output_default(line);
				count++;
			}

			free(line);
		}

		lines--;
	}

	fclose(fp);

	return count;
}


/* Search using regular expressions (POSIX Basic Regular Expression syntax)
 * Returns the count of found notes or -1 if functions fails.
 */
static int search_regexp(const char *regexp)
{
	int count = 0;
	regex_t regex;
	int ret;
	char *line = NULL;
	char lines = 0;
	FILE *fp = NULL;
	char buffer[100];

	ret = regcomp(&regex, regexp, REG_ICASE);

	if (ret != 0) {
		fail(stderr, "%s: invalid regexp\n", __func__);
		return -1;
	}

	fp = get_memo_file_ptr("r");
	lines = count_file_lines(fp);

	if (lines == -1) {
		regfree(&regex);
		fail(stderr,"%s: counting lines failed\n", __func__);
		return -1;
	}

	while (lines >= 0) {
		line = read_file_line(fp);

		if (line) {
			ret = regexec(&regex, line, 0, NULL, 0);

			if (ret == 0) {
				output_default(line);
				count++;
			} else if (ret != 0 && ret != REG_NOMATCH) {
				/* Something went wrong while executing
				   regexp. Clean up and exit loop. */
				regerror(ret, &regex, buffer, sizeof(buffer));
				fail(stderr, "%s: %s\n", __func__, buffer);
				free(line);

				break;
			}

			free(line);
		}

		lines--;
	}

	regfree(&regex);
	fclose(fp);

	return count;
}


/* Replace note status old with new status in line.*/
static void note_status_replace(char *line, char old, char new)
{
	while (*line) {
		if (*line == old) {
			*line = new;
			break;
		}
		line++;
	}
}


/* Get the note status from the note line.
 * Returns STATUS_ERROR on failure.
 */
static NoteStatus_t get_note_status(const char *line)
{
	char *token = NULL;
	char *buffer = NULL;
	NoteStatus_t status;

	status = STATUS_ERROR;

	/* Sanity check for an empty line */
	if(strlen(line) == 0)
		return status;

	buffer = (char*)malloc((strlen(line) + 1) * sizeof(char));

	if (buffer == NULL) {
		fail(stderr, "%s malloc failed\n", __func__);
		return status;
	}

	strcpy(buffer, line);

	token = strtok(buffer, "\t");
	token = strtok(NULL, "\t");

	if (token == NULL) {
		fail(stderr, "%s: parsing line failed\n", __func__);
		free(buffer);
		return status;
	}

	if (strcmp(token, "U") == 0)
		status = UNDONE;
	else if (strcmp(token, "D") == 0)
		status = DONE;
	else if (strcmp(token, "P") == 0)
		status = POSTPONED;

	free(buffer);

	return status;
}


/* Simple helper function to mark note as done */
static void mark_as_done(FILE *fp, char *line)
{
	if (get_note_status(line) == POSTPONED)
		note_status_replace(line, 'P', 'D');
	else
		note_status_replace(line, 'U', 'D');

	fprintf(fp, "%s\n", line);
}


/* Simple helper function to mark note as undone */
static void mark_as_undone(FILE *fp, char *line)
{
	if (get_note_status(line) == POSTPONED)
		note_status_replace(line, 'P', 'U');
	else
		note_status_replace(line, 'D', 'U');

	fprintf(fp, "%s\n", line);
}


/* Simple helper function to mark note as postponed */
static void mark_as_postponed(FILE *fp, char *line)
{
	/* Only UNDONE notes can be postponed */
	if (get_note_status(line) == UNDONE) {
		note_status_replace(line, 'U', 'P');
		fprintf(fp, "%s\n", line);
	} else {
		fprintf(fp, "%s\n", line);
	}
}


/* Mark note by status U is undone, D is done or P
 * postponed. When status is DELETE, the note with
 * a matching id will be deleted.
 *
 * Function will create a temporary file to write
 * the memo file with new changes. Then the original
 * file is replaced with the temp file.

 * id is ignored when status is DELETE_DONE or ALL_DONE.
 */
static int mark_note_status(NoteStatus_t status, int id)
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
	lines = count_file_lines(fp);

	if (lines == -1) {
		free(memofile);
		fclose(tmpfp);
		fail(stderr,"%s: counting lines failed\n", __func__);
		return -1;
	}

	while (lines >= 0) {
		line = read_file_line(fp);

		if (line) {
			char *endptr;
			int curr = strtol(line, &endptr, 10);

			switch(status) {

			case DONE:
				if (curr == id)
					mark_as_done(tmpfp, line);
				else
					fprintf(tmpfp, "%s\n", line);
				break;
			case UNDONE:
				if (curr == id)
					mark_as_undone(tmpfp, line);
				else
					fprintf(tmpfp, "%s\n", line);
				break;
			case DELETE:
				/* Write all the other lines, except the one
				 * with the matching id. This is a simple way
				 * to delete the line from the file.
				 */
				if (curr != id)
					fprintf(tmpfp, "%s\n", line);
				break;
			case DELETE_DONE:
				if (get_note_status(line) != DONE)
					fprintf(tmpfp, "%s\n", line);
				break;
			case STATUS_ERROR:
				fail(stderr,"STATUS_ERROR, this shouldn't happen\n");
				break;
			case ALL_DONE:
				note_status_replace(line, 'U', 'D');
				fprintf(tmpfp, "%s\n", line);
				break;
			case POSTPONED:
				if (curr == id)
					mark_as_postponed(tmpfp, line);
				else
					fprintf(tmpfp, "%s\n", line);
				break;
			}

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


/* This functions handles the output of one line.
 * Postponed notes are ignored.
 */
static void output_default(char *line)
{
	if (get_note_status(line) != POSTPONED)
		printf("%s\n", line);
}


/* Output notes which are postponed.
 * Called from show_notes when command line option -P
 * is used.
 */
static void output_postponed(char *line)
{
	if (get_note_status(line) == POSTPONED)
		printf("%s\n", line);
}


/* Export current .memo file to a html file
 * Return the path of the html file, or NULL on failure.
 */
static const char *export_html(const char *path)
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
	lines = count_file_lines(fpm);

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
		line = read_file_line(fpm);

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
static void show_latest(int n)
{
	FILE *fp = NULL;
	char *line;
	int lines = 0;
	int start;
	int current = 0;

	fp = get_memo_file_ptr("r");

	lines = count_file_lines(fp);

	if (lines != -1) {
		/* If n is bigger than the count of lines or smaller
		 * than zero we will show all the lines.
		 */
		if (n > lines || n < 0)
			start = -1;
		else
			start = lines - n;

		while (lines >= 0) {
			line = read_file_line(fp);

			if (line) {
				if (current > start)
					printf("%s\n", line);
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
 * simply removes .memo file.
 * Returns 0 on success, -1 on failure.
 */
static int delete_all()
{
	char *confirm = NULL;
	int ask = 1;

	confirm = get_memo_conf_value("MEMO_CONFIRM_DELETE");

	if (confirm) {

		if (strcmp(confirm, "no") == 0)
			ask = 0;

		free(confirm);
	}

	char *path = get_memo_file_path();

	if (path == NULL) {
		fail(stderr,"%s error getting .memo file path\n", __func__);
		return -1;
	}

	if (ask) {
		printf("Really delete (y/N)? ");
		char ch = getc(stdin);
		if (ch == 'y' || ch == 'Y') {
			if (remove(path) != 0) {
				fail(stderr, 
					"%s error removing %s\n", __func__, 
					path);
			}
		}
	} else {
		if (remove(path) != 0)
			fail(stderr,"%s error removing %s\n", __func__, path);
	}

	free(path);

	return 0;
}


/* Delete a note by id.
 * Returns 0 on success and -1 on failure.
 */
static int delete_note(int id)
{
	return mark_note_status(DELETE, id);
}


/* Return the path to $HOME/.memorc.
 * On failure NULL is returned.
 * Caller is responsible for freeing the return value.
 */
static char *get_memo_conf_path()
{
	char *env = NULL;
	char *conf_path = NULL;
	size_t len = 0;

	env = getenv("HOME");

	if (env == NULL){
		fail(stderr,"%s: getenv(\"HOME\") failed\n", __func__);
		return NULL;
	}

	/* +1 for \0 byte */
	len = strlen(env) + 1;

	/* +8 to have space for \"/.memorc\" */
	conf_path = (char*)malloc( (len + 8) * sizeof(char));

	if (conf_path == NULL) {
		fail(stderr, "%s: malloc failed\n", __func__);
		return NULL;
	}

	strcpy(conf_path, env);
	strcat(conf_path, "/.memorc");

	return conf_path;
}


/* ~/.memorc file format is following:
 *
 * PROPERTY=value
 *
 * e.g MEMO_PATH=/home/niko/.memo
 *
 * This function returns the value of the property.
 * NULL is returned on failure.
 * On success, caller must free the return value.
 */
static char *get_memo_conf_value(const char *prop)
{
	char *retval = NULL;
	char *conf_path = NULL;
	FILE *fp = NULL;

	conf_path = get_memo_conf_path();

	if (conf_path == NULL)
		return NULL;

	fp = fopen(conf_path, "r");

	if (fp == NULL) {
		free(conf_path);
		return NULL;
	}

	int lines = count_file_lines(fp);

	if (lines == -1) {
		fail(stderr, "%s: counting lines failed\n", __func__);
		fclose(fp);
		free(conf_path);

		return NULL;
	}

	while (lines >= 0) {

		char *line = read_file_line(fp);

		if (line) {
			if (strncmp(line, prop, strlen(prop)) == 0) {

				/* Property found, get the value */
				char *token = strtok(line, "=");
				token = strtok(NULL, "=");

				if (token == NULL) {
					/* property does not have
					 * a value. fail.
					 */
					fail(stderr, "%s: no value\n", prop);
					free(line);

					break;
				}

			        size_t len = strlen(token) + 1;
				retval = (char*)malloc(len * sizeof(char));

				if (retval == NULL) {
					fail(stderr,"%s malloc\n", __func__);
					free(line);

					break;
				}

				strcpy(retval, token);
				free(line);

				break;

			}

			free(line);
		}

		lines--;
	}

	fclose(fp);
	free(conf_path);

	return retval;
}


/* Returns the default path
 * Default path is ~/.memo
 * 
 * Caller must free the return value.
 * On failure NULL is returned.
 */
static char *get_memo_default_path()
{
	char *path = NULL;
	char *env = getenv("HOME");
	size_t len = 0;

	if (env == NULL){
		fail(stderr,"%s: getenv(\"HOME\") failed\n", __func__);
		return NULL;
	}

	/* +1 for \0 byte */
	len = strlen(env) + 1;

	/* +6 to have space for \"/.memo\" */
	path = (char*)malloc( (len + 6) * sizeof(char));

	if (path == NULL) {
		fail(stderr,"%s: malloc failed\n", __func__);
		return NULL;
	}

	strcpy(path, env);
	strcat(path, "/.memo");

	return path;
}


/* Function reads MEMO_PATH environment variable to see
 * if it's set and uses value from it as a path.
 * When MEMO_PATH is not set, function reads $HOME/.memorc file.
 * If the file is not found $HOME/.memo is used as a fallback
 * path.
 *
 * Returns the path to .memo file or NULL on failure.
 * Caller is responsible for freeing the return value.
 */
static char *get_memo_file_path()
{
	char *path = NULL;
	char *env_path = NULL;

	env_path = getenv("MEMO_PATH");
	/* Try and see if environment variable MEMO_PATH is set
	 * and use value from it as a path */
	if (env_path != NULL) {
		/* +1 for \0 byte */
		path = (char*)malloc((strlen(env_path) + 1) * sizeof(char));

		if (path == NULL) {
			fail(stderr, "%s malloc failed\n", __func__);
			return NULL;
		}
		strcpy(path, env_path);

		return path;
	}

	char *conf_path = NULL;

	conf_path = get_memo_conf_path();

	if (conf_path == NULL)
		return NULL;


	if (access(conf_path,F_OK) != 0) {
		/* Config file not found, so fallback to ~/.memo */
		path = get_memo_default_path();

	} else {
		/* Configuration file found, read .memo location
		   from it */
		path = get_memo_conf_value("MEMO_PATH");
		
		if (path == NULL) {
			/* Failed to get the path. Most likely user did not
			 * specify MEMO_PATH in the configuration file at all
			 * and configuration file is used for setting other
			 * properties like MEMO_CONFIRM_DELETE.
			 *
			 * Let's default to ~/.memo
			 */
			path = get_memo_default_path();
		}

	}

	free(conf_path);

	return path;
}


/* Returns temporary .memo.tmp file.
 * It will be in the same directory
 * as the original .memo file.
 *
 * Returns NULL on failure.
 */
static char *get_temp_memo_path()
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


/* .memo file format is following:
 *
 * id     status     date           content
 * |      |          |              |
 * |- id  |-U/D/P    |- yyy-MM-dd   |- actual note
 *
 * sections are separated by a tab character
 *
 * Parameter date can be NULL. If date is given
 * in valid format(yyyy-MM-dd) it will be used
 * for creating the note. If date is NULL, current
 * date will be used instead.
 *
 * Note will be marked with status "U" which means it's "undone".
 * "D" means "done". With status P, note is marked as postponed.
 */
static int add_note(const char *content, const char *date)
{
	FILE *fp = NULL;
	time_t t;
	struct tm *ti;
	int id = -1;
	char note_date[11];

	/* Do not add an empty note */
	if (strlen(content) == 0)
		return -1;

	fp = get_memo_file_ptr("a");

	if (fp == NULL) {
		fail(stderr,"%s: Error opening ~/.memo\n", __func__);
		return -1;
	}

	id = get_next_id();

	if (id == -1)
		id = 1;

	if (date != NULL) {
                /* Date is already validated, so just copy it
		 * for later use.
		 */
		strcpy(note_date, date);
	} else {

		time(&t);
		ti = localtime(&t);

		strftime(note_date, 11, "%Y-%m-%d", ti);
	}


	fprintf(fp, "%d\t%s\t%s\t%s\n", id, "U", note_date,
		content);

	fclose(fp);

	return id;
}


static void usage()
{
#define HELP "\
SYNOPSIS\n\
\n\
    memo [options]\n\
\n\
OPTIONS\n\
\n\
    -a <content> [yyyy-MM-dd]    Add a new note with optional date\n\
    -d <id>                      Delete note by id\n\
    -D                           Delete all notes\n\
    -e <path>                    Export notes as html to a file\n\
    -f <search>                  Find notes by search term\n\
    -F <regex>                   Find notes by regular expression\n\
    -l <n>                       Show latest n notes\n\
    -m <id>                      Mark note status as done\n\
    -M <id>                      Mark note status as undone\n\
    -p                           Show current memo file path\n\
    -P [id]                      Show postponed or mark note as postponed\n\
    -R                           Delete all notes marked as done\n\
    -s                           Show all notes except postponed\n\
    -T                           Mark all notes as done\n\
\n\
    -h                           Show short help and exit. This page\n\
    -V                           Show version number of program\n\
\n\
DESCRIPTION\n\
\n\
    Memo is a note-taking software for POSIX compatible operating systems.\n\
    The short notes are saved to user's home directory in ~/.memo file\n\
    by default.\n\
\n\
EXAMPLES\n\
\n\
    Add a new note:\n\
        memo -a \"Remember to buy milk!\"\n\
\n\
    Add a new note with custom date:\n\
        memo -a \"File taxes \" 2014-10-14\n\
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
    Mark note as postponed:\n\
       memo -P 4\n\
\n\
    Show postponed notes:\n\
      memo -P\n\
\n\
    Postponed notes are not shown with -s. Postponed note\n\
    can be marked as undone or done normally using -m or -M flag.\n\
    Only undone notes can be postponed.\n\
\n\
    It's possible to change the location (and name) of the .memo file.\n\
    Create $HOME/.memorc with a line MEMO_PATH=/path/you/would/like,\n\
    Memo will use that path instead of the default $HOME/.memo path.\n\
    Memo path can be also set using an environment variable MEMO_PATH.\n\
    For example: export MEMO_PATH=/path/to/some/file\n\
\n\
AUTHORS\n\
    Copyright (C) 2014 Niko Rosvall <niko@ideabyte.net>\n\
\n\
    Released under license GPL-3+. For more information, see\n\
    http://www.gnu.org/licenses\n\
"

	printf(HELP);
}


static void show_memo_file_path()
{
	char *path = NULL;

	path = get_memo_file_path();

	if (path == NULL) {
		fail(stderr,"%s: can't retrieve path\n", __func__);
	}
	else {
		printf("%s\n", path);
		free(path);
	}
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

		if (fd == -1) {
			fail(stderr,"%s: failed to create empty memo\n",
				__func__);
			free(path);
			return -1;
		}

		close(fd);
	}

	opterr = 0;

	if (argc == 1) {
	       /* No options available, so get data from stdin.
		* Assumes that the data is content for a new note.
		*/
		stdinline = read_file_line(stdin);

		if (stdinline) {
			add_note(stdinline, NULL);
			free(stdinline);
		}
	}

	while ((c = getopt(argc, argv, "a:d:De:f:F:hl:m:M:pPRsTV")) != -1){
		has_valid_options = 1;

		switch(c){
		case 'a':
			if (argv[optind]) {
				if (is_valid_date_format(argv[optind]) == 0)
					add_note(optarg, argv[optind]);
			}
			else {
				add_note(optarg,NULL);
			}
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
		case 'F':
			search_regexp(optarg);
			break;
		case 'h':
			usage();
			break;
		case 'l':
			show_latest(atoi(optarg));
			break;
		case 'm':
			mark_note_status(DONE, atoi(optarg));
			break;
                case 'M':
	                mark_note_status(UNDONE, atoi(optarg));
	                break;
		case 'p':
			show_memo_file_path();
			break;
		case 'P':
			if (argv[optind])
				mark_note_status(POSTPONED, atoi(argv[optind]));
			else
				show_notes(POSTPONED);
			break;
		case 'R':
			mark_note_status(DELETE_DONE, -1);
			break;
		case 's':
			show_notes(-1);
			break;
		case 'T':
			mark_note_status(ALL_DONE, -1);
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
			else if (optopt == 'F')
				printf("-F missing an argument <regex>\n");
			else if (optopt == 'l')
				printf("-l missing an argument <id>\n");
			else if (optopt == 'm')
				printf("-m missing an argument <id>\n");
			else if(optopt == 'M')
				printf("-M missing an argument <id>\n");
			else
				printf("invalid option, see memo -h for help\n");
			break;
		}
	}

	if (argc > 1 && !has_valid_options)
		printf("invalid input, see memo -h for help\n");

	free(path);

	return 0;
}
