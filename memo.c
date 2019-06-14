/* Memo is a Unix-style note-taking software.
 *
 * Copyright (C) 2014-2019 Niko Rosvall <niko@byteptr.com>
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
 * Memo should be fairly portable for POSIX systems.
 */

/* To enable _POSIX_C_SOURCE feature test macro */
#define _XOPEN_SOURCE 600

/* Make only POSIX.2 regexp functions available */
#define _POSIX_C_SOURCE 200112L

#ifdef _WIN32
# undef __STRICT_ANSI__
# define S_IRGRP 0
# define S_IROTH 0
#endif

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/types.h>
#ifdef _WIN32
# include <pcreposix.h>
#else
# include <regex.h>
#endif
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


/* NOTE_STATUS part is handled by NoteStatus_t
 * in function mark_note_status.
 */
typedef enum {
	NOTE_DATE = 1,
	NOTE_CONTENT = 2,
	NOTE_ID = 3
} NotePart_t;


/* Function declarations */
static char *read_file_line(FILE *fp);
static int  add_notes_from_stdin();
static char *get_memo_file_path();
static char *get_memo_default_path();
static char *get_memo_conf_path();
static char *get_temp_memo_path();
static char *get_memo_conf_value(const char *prop);
static int   is_valid_date_format(const char *date, int silent_errors);
static int   file_exists(const char *path);
static void  remove_content_newlines(char *content);
static int   add_note(char *content, const char *date);
static int   replace_note(int id, const char *data);
static int   get_next_id();
static char *get_note_date(char *line);
static int   get_note_id_from_line(const char *line);
static char *integer_to_string(int id);
static int   delete_note(int id);
static int   show_notes(NoteStatus_t status);
static int   show_notes_tree();
static int   count_file_lines(FILE *fp);
static char  *note_part_replace(NotePart_t part, char *note_line, const char *data);
static char  *case_strstr(const char *str1, const char *str2);
static int   search_notes(char *search);
static int   search_regexp(const char *regexp);
static const char *export_html(const char *path);
static const char *export_csv(const char *path);
static void  output(char *line, int is_odd_line);
static void  output_default(char *line, int is_odd_line);
static void  output_undone(char *line, int is_odd_line);
static void  output_postponed(char *line, int is_odd_line);
static void  output_without_date(char *line, int is_odd_line);
static void  show_latest(int count);
static FILE *get_memo_file_ptr(char *mode);
static FILE *get_memo_tmpfile_ptr();
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
static int   mark_old_as_done();
static int   organize_note_identifiers();
static char *get_line_color(int is_odd_line);
static char *color_to_escape_seq(char *color);
static int  is_odd(int n);
static void sort_dates_ascend(char *dates[], int date_index);
static int  int_sort(const void *a , const void *b);

#define VERSION 1.7


/* Check if given date is in valid date format.
 * Memo assumes the date format to be yyyy-MM-dd.
 *
 * If silent_errors is 1, no error information will be outputted.
 * When silent_errors != 1, error information is outputted to stderr.
 *
 * Functions returns 0 on success and -1 on failure.
 */
static int is_valid_date_format(const char *date, int silent_errors)
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
		if ( !silent_errors)
			fail(stderr,"%s: invalid date format\n", __func__);

		return -1;
	}

	/* Leap year check */
	if (y % 400 == 0 || y % 100 != 0 || y % 4 == 0)
		day_count[1] = 29;

	if (m < 13 && m > 0) {
		if (d <= day_count[m - 1]) {
			return 0;
		}
		else {
			if (!silent_errors)
				fail(stderr, "%s: invalid day\n", __func__);
		}
	} else {
		if (!silent_errors)
			fail(stderr, "%s: invalid month\n", __func__);
	}

	if (!silent_errors)
		fail(stderr, "%s: parsing date failed\n", __func__);

	return -1;
}


/* Functions checks if file exists.
 * This should be more reliable than using access().
 *
 * Returns 1 when the file is found and 0
 * when it's not found.
 *
 * Please note that 0 could be returned even if
 * the file exists but there are other problems accessing it.
 * See http://pubs.opengroup.org/onlinepubs/009695399/functions/stat.html
 */
static int file_exists(const char *path)
{
	int retval = 0;
	struct stat buffer;

	if (stat(path, &buffer) == 0)
		retval = 1;

	return retval;
}


/* This function is used to count lines in .memo and ~/.memorc
 * files.
 *
 * Count the lines of the file as a note is always one liner,
 * lines == note count.
 *
 * File pointer is rewinded to the beginning of the file.
 *
 * Returns the count or -1 if the file pointer is null. -2 is
 * returned when there are no lines.
 *
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
	if (count == 0)
		return -2;

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


/* Get a FILE descriptor for temp file.
 * Caller is responsible for closing the
 * file pointer.
 *
 * Return NULL on failure.
 */
static FILE *get_memo_tmpfile_ptr()
{
	FILE *fp = NULL;
	char *tmp = NULL;

	tmp = get_temp_memo_path();

	if (tmp == NULL) {
		fail(stderr, "%s: error getting a temp file\n", __func__);
		return NULL;
	}

	fp = fopen(tmp, "w");

	if (fp == NULL) {
		fail(stderr, "%s: error opening temp file\n", __func__);
		free(tmp);
		return NULL;
	}

	free(tmp);

	return fp;
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


/* Function reads multiple lines from stdin until
 * the end of transmission (^D).
 *
 * Each line is assumed to be the content part of the note.
 *
 * Notes are added to the memo file. Returns -1 on failure, 0 on
 * success.
 */
static int add_notes_from_stdin()
{
	/* First get the whole buffer from stdin. Then parse the buffer;
	 * each note is separated by a new line character in the
	 * buffer. Call add_note for each line.
	 */
	int length = 128;
	char *buffer = NULL;
	int count = 0;
	char ch;
	char *line = NULL;

	if ((buffer = malloc(sizeof(char) * length)) == NULL) {
		fail(stderr, "%s: malloc failed\n", __func__);
		return -1;
	}

	ch = getc(stdin);

	while (ch != EOF) {
		if (count == length) {
			length += 128;
			if ((buffer = realloc(buffer, length)) == NULL) {
				fail(stderr, "%s realloc failed\n", __func__);
				return -1;
			}
		}

		buffer[count] = ch;
		count++;
		ch = getc(stdin);
	}

	buffer[count] = '\0';
	buffer = realloc(buffer, count + 1);

	line = strtok(buffer, "\n");

	while (line != NULL) {
		add_note(line, NULL);
		line = strtok(NULL, "\n");
	}

	free(buffer);

	return 0;
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

	buffer = malloc(sizeof(char) * length);

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
 * If the file is missing or is empty, return 1
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

	if (lines == -2) {
		fclose(fp);
		return id + 1;
	}

	while (1) {
		line = read_file_line(fp);

		/* Check if we're at the last line */
		if (line && current == lines) {
			id = get_note_id_from_line(line);
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
 * notes are shown. With status UNDONE, only undone
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

	/* Output count is used to calculate even and odd lines
	 * when outputting undone or postponed notes.
	 * When outputting normally, lines is used as the counter.
	 */
	int postponed_output_count = 0;
	int undone_output_count = 0;

	fp = get_memo_file_ptr("r");

	lines = count_file_lines(fp);
	count = lines;

	if (lines == -1) {
		fail(stderr,"%s: counting lines failed\n", __func__);
		return -1;
	}

	/* Ignore empty note file and exit */
	if (lines == -2) {
		fail(stderr,"You don't have any notes currently.\n", __func__);
		fclose(fp);
		return -1;
	}

	while (lines >= 0) {
		line = read_file_line(fp);

		if (line) {

			/* status is just used to know what kind of output we want.
			 * we still need to check status the of the current line
			 * separately.
			 */
			if (status == POSTPONED) {
				if (get_note_status(line) == POSTPONED) {
					postponed_output_count++;
					int odd_p = is_odd(postponed_output_count);
					output_postponed(line, odd_p);
				}
			}
			else if (status == UNDONE) {
				if (get_note_status(line) == UNDONE) {
					undone_output_count++;
					int odd_u = is_odd(undone_output_count);
					output_undone(line, odd_u);
				}
			}
			else {
				output_default(line, is_odd(lines));
			}

			free(line);
		}

		lines--;
	}

	fclose(fp);

	return count;
}


/* Function returns the date string of the note.
 *
 * On success caller must free the return value.
 * NULL is returned on failure.
 */
static char *get_note_date(char *line)
{
	char *date = NULL;
	char *tmpline = strdup(line);
	char *datetoken = NULL;

	if (tmpline == NULL)
		return NULL;

	datetoken = strtok(tmpline, "\t");
	datetoken = strtok(NULL, "\t");
	datetoken = strtok(NULL, "\t");

	if (datetoken == NULL) {
		free(tmpline);
		return NULL;
	}

	date = malloc((strlen(datetoken) + 1) * sizeof(char));

	if (date == NULL) {
		free(tmpline);
		return NULL;
	}

	strcpy(date, datetoken);

	free(tmpline);

	return date;
}


/* Function displays notes ordered by date.
 *
 * For example:
 *
 *   2014-11-01
 *         1   U   Release Memo 1.3
 *         2   D   Pay rent
 *   2014-11-02
 *         3   D   Go shopping
 *
 * Returns the count of the notes. On failure returns -1.
 */
static int show_notes_tree()
{
	int count = 0;
	int lines = 0;
	FILE *fp = NULL;
	int date_index = 0;

	fp = get_memo_file_ptr("r");
	lines = count_file_lines(fp);

	if (lines == -1) {
		fail(stderr, "%s: counting lines failed\n", __func__);
		return -1;
	}

	/* Ignore empty note file and exit */
	if (lines == -2) {
		fclose(fp);
		return -1;
	}

	int n = lines;
	char *dates[lines + 1];

	memset(dates, 0, sizeof(dates));

	/* Get the date of each note and store the pointer
	 * of it to dates array
	 */
	while (n >= 0) {
		char *line = read_file_line(fp);
		if (line) {
			char *date = get_note_date(line);
			int has_date = 0;

			if (date == NULL) {
				free(line);
				fclose(fp);
				fail(stderr, "%s problem getting date\n",
					__func__);
				return -1;
			}
			/* Prevent storing duplicate dates*/
			for (int i = 0; i < date_index; i++) {
				if (dates[i]) {
					if (strcmp(dates[i], date) == 0) {
						has_date = 1;
						break;
					}
				}
			}

			/* If dates does not contain date, store it
			 * otherwise free it
			 */
			if (!has_date) {
				dates[date_index] = date;
				date_index++;
			} else {
				free(date);
			}

			count++;
			free(line);
		}
		n--;
	}

	/* Sort the dates elements in the order of date. */
	sort_dates_ascend(dates, date_index);

	/* Loop through all dates and print all notes for
	 * the date.
	 */
	for (int i = 0; i <= lines; i++) {
		/* Rewind file pointer to beginning every time to
		 * loop all the notes in the file.
		 * It's possible that the array is not fully populated (because
		 * it's allocated to have as many elements as we have notes,
		 * however duplicate dates are not stored at all.
		 * so do a check if the value is null.
		 */
		if (dates[i]) {
			n = lines;
			rewind(fp);
			printf("%s\n", dates[i]);
			while (n >= 0) {
				char *line = read_file_line(fp);
				if (line) {
					char *date = get_note_date(line);
					if (date == NULL) {
						free(line);
						fclose(fp);
						return -1;
					}

					if (strcmp(date, dates[i]) == 0)
						output_without_date(line, is_odd(i));

					free(line);
					free(date);
				}
				n--;
			}
			free(dates[i]);
		}
	}

	fclose(fp);

	return count;
}


/* Function works like strstr, but ignores the case.
 * There's a function strcasestr, but it's nonstandard
 * GNU extension, so let's not use that.
 *
 * Return value must be freed by the caller.
 */
static char *case_strstr(const char *str1, const char *str2)
{
	char *tmp1 = NULL;
	char *tmp2 = NULL;
	char *tmp3 = NULL;
	char *retval = NULL;

	tmp1 = strdup(str1);

	if (tmp1 == NULL) {
		fail(stderr, "%s: strdup failed\n", __func__);
		return NULL;
	}

	tmp2 = strdup(str2);

	if (tmp2 == NULL) {
		free(tmp1);
		fail(stderr, "%s: strdup failed\n", __func__);
		return NULL;
	}

	for (int i = 0; i < strlen(tmp1); i++)
		tmp1[i] = tolower(tmp1[i]);

	for (int i = 0; i < strlen(tmp2); i++)
		tmp2[i] = tolower(tmp2[i]);

	tmp3 = strstr(tmp1, tmp2);

	if (tmp3) {
		retval = strdup(tmp3);
		/* Sanity check
		 * Inform the user that something went wrong
		 * even the search term was found. Probably never happens.
		 */
		if (retval == NULL) {
			fail(stderr,"%s: search term found, but strdup fails\n",
				__func__);
		}
	}

	free(tmp1);
	free(tmp2);

	return retval;
}


/* Search if a note contains the search term.
 * Returns the count of found notes or -1 if function fails.
 */
static int search_notes(char *search)
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

	/* Ignore empty note file and exit */
	if (lines == -2) {
		fclose(fp);
		return -1;
	}

	while (lines >= 0) {
		line = read_file_line(fp);

		if (line) {
			char *token = NULL;
			token = strtok(search, " ");
			/* Loop through each word in the search string
			 * and see if any of the words can be found
			 */
			while (token != NULL) {
				char *foundptr = case_strstr(line, token);

				if (foundptr){
					output_default(line, is_odd(count));
					count++;
					free(foundptr);
					/* found it, no point to continue */
					break;
				}

				token = strtok(NULL, " ");
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

	/* Ignore empty note file and exit */
	if (lines == -2) {
		regfree(&regex);
		return -1;
	}

	while (lines >= 0) {
		line = read_file_line(fp);

		if (line) {
			ret = regexec(&regex, line, 0, NULL, 0);

			if (ret == 0) {
				output_default(line, is_odd(count));
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

	buffer = malloc((strlen(line) + 1) * sizeof(char));

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


/* Returns hte id of the short note. On failure, returns -1. */
static int get_note_id_from_line(const char *line)
{
	int id = -1;

	char *endptr;
	id = strtol(line, &endptr, 10);

	return id;
}


/* Mark note by status U is undone, D is done or P postponed. When
 * status is DELETE, the note with a matching id will be deleted.
 *
 * Function will create a temporary file to write the memo file with new
 * changes. Then the original file is replaced with the temp file.

 * id is ignored when status is DELETE_DONE or ALL_DONE.
 */
static int mark_note_status(NoteStatus_t status, int id)
{
	FILE *fp = NULL;
	FILE *tmpfp = NULL;
	char *line = NULL;
	char *tmp;
	int lines = 0;

	fp = get_memo_file_ptr("r");
	lines = count_file_lines(fp);

	if (lines == -1) {
		fail(stderr,"%s: counting lines failed\n", __func__);
		return -1;
	}

	/* Ignore empty note file and exit */
	if (lines == -2) {
		fclose(fp);
		printf("Nothing to do. No notes found\n");
		return -1;
	}

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


	while (lines >= 0) {
		line = read_file_line(fp);

		if (line) {

			int curr = get_note_id_from_line(line);

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

	if (file_exists(memofile))
		remove(memofile);

	rename(tmp, memofile);
	remove(tmp);

	free(memofile);
	free(tmp);

	return 0;
}


/* Function reads ~/.memorc for MARK_AS_DONE property
 * and marks all notes older than the property value as DONE.
 *
 * For example if you have MARK_AS_DONE=2014-12-13, this function
 * will mark all notes older than 2014-12-13 as DONE.
 *
 * Returns count of marked notes on success, -1 on failure.
 */
static int mark_old_as_done()
{
#ifdef _WIN32
	return -1;
#endif
	char *conf_path = NULL;
	FILE *fp;
	char *date = NULL;
	struct tm dc = {0};
	time_t date_to_compare;
	int lines = 0;

	conf_path = get_memo_conf_path();

	if (conf_path == NULL)
		return -1;

	if (!file_exists(conf_path)) {

		free(conf_path);

		return -1;
	}

	date = get_memo_conf_value("MARK_AS_DONE");

	if (date == NULL) {
		free(conf_path);
		return -1;
	}

	if (is_valid_date_format(date, 0) == -1) {

		fail(stderr, "%s: error in ~/.memorc parsing\n", __func__);
		free(date);
		free(conf_path);

		return -1;
	}

	fp = get_memo_file_ptr("r");
	lines = count_file_lines(fp);

	if (lines == -1) {

		free(date);
		free(conf_path);

		if (lines == -2)
			fclose(fp);

		return -1;
	}

	int id_array[lines];
	int id_count = 0;

	/* Convert MARK_AS_DONE property value string
	 * to a valid date */
	strptime(date, "%Y-%m-%d", &dc);
	date_to_compare = mktime(&dc);

	/* Compare each note line date with the one read
	 * from the ~/.memorc and store note id for later use
	 * if note date is older
	 */
	while (lines >= 0) {
		char *line = read_file_line(fp);

		if (line) {

			int id = get_note_id_from_line(line);
			char *curr_date = get_note_date(line);

			if (curr_date == NULL) {
				free(line);
				free(date);
				free(conf_path);
				fclose(fp);
			}

			time_t note_time;
			struct tm nt = {0};

			strptime(curr_date, "%Y-%m-%d", &nt);
			note_time = mktime(&nt);

			/* Store id codes of notes we want to mark as done
			 * as we can't modify the file while we're reading it
			 */
			if (difftime(date_to_compare, note_time) > 0) {
				id_array[id_count] = id;
				id_count++;
			}

			free(curr_date);
		}

		free(line);

		lines--;
	}

	free(date);
	free(conf_path);
	fclose(fp);

	/* Loop through all the note id codes and mark corresponding
	 * notes as DONE
	 */
	for (int i = 0; i < id_count; i++)
		mark_note_status(DONE, id_array[i]);

	return 0;
}


/* Function returns the corresponding terminal
 * escape sequence of the color.
 */
static char *color_to_escape_seq(char *color)
{
	#define red "\033[0;31m"
	#define cyan "\033[0;36m"
	#define green "\033[0;32m"
	#define blue "\033[0;34m"
	#define black "\033[0;30m"
	#define brown "\033[0;33m"
	#define magenta "\033[0;35m"
	#define gray "\033[0;37m"
	#define none "\033[0m"

	if (strcmp(color, "red") == 0)
		return red;
	else if (strcmp(color, "cyan") == 0)
		return cyan;
	else if (strcmp(color, "green") == 0)
		return green;
	else if (strcmp(color, "blue") == 0)
		return blue;
	else if (strcmp(color, "black") == 0)
		return black;
	else if (strcmp(color, "brown") == 0)
		return brown;
	else if (strcmp(color, "magenta") == 0)
		return magenta;
	else if (strcmp(color, "gray") == 0)
		return gray;

	return none;
}


/* Function returns 1 if n is an even number.
 * 0 is returned if n is odd.
 */
static int is_odd(int n)
{
	if ( n % 2)
		return 1;

	return 0;
}


/* Returns the color value of LINE_COLOR or ODD_LINE_COLOR in .memorc.
 * This function returns NULL if USE_COLORS is set to false.
 *
 * Use is_odd_line=0 to return the value of LINE_COLOR
 * property. Use is_odd_line=1 to get the value of ODD_LINE_COLOR.
 *
 * If LINE_COLOR/ODD_LINE_COLOR is not set, but USE_COLORS is set to true
 * function will return the default color for the line.
 *
 * Not supported on Windows, returns always NULL.
 *
 */
static char *get_line_color(int is_odd_line)
{

#ifdef _WIN32
	return NULL;
#endif

	char *usecolors = NULL;
	char *color = NULL;
	char *defaultclr = NULL;

	/* Default LINE_COLOR/ODD_LINE_COLOR. */
	defaultclr = "magenta";

	if (is_odd_line)
		defaultclr = "blue";

	usecolors = get_memo_conf_value("USE_COLORS");

	if (!usecolors)
		return NULL;

	if (strcmp(usecolors, "no") == 0) {
		free(usecolors);
		return NULL;
	}

	if (is_odd_line)
		color = get_memo_conf_value("ODD_LINE_COLOR");
	else
		color = get_memo_conf_value("LINE_COLOR");

	if (!color) {
		color = malloc((strlen(defaultclr) + 1) * sizeof(char));
		if (!color) {
			fail(stderr, "%s malloc failed\n", __func__);
			return NULL;
		}

		strcpy(color, defaultclr);
	}

	char *value = color_to_escape_seq(color);
	free(color);
	free(usecolors);

	return value;
}

static void output(char *line, int is_odd_line)
{
	char *color = NULL;

	color = get_line_color(is_odd_line);

	if (!color) {
		printf("%s\n", line);
	} else {
		printf("%s%s\n", color, line);
		/* Reset terminal colors */
		printf("\033[0m");
	}
}


/* This functions handles the output of one line.
 * Postponed notes are ignored.
 *
 * Set is_odd_line to 1 if the line to be outputted is odd.
 */
static void output_default(char *line, int is_odd_line)
{
	if (get_note_status(line) != POSTPONED)
		output(line, is_odd_line);
}


/* Output notes which are postponed.
 * Called from show_notes when command line option -P
 * is used.
 *
 * Set is_odd_line to 1 if the line to be outputted is odd.
 */
static void output_postponed(char *line, int is_odd_line)
{
	if (get_note_status(line) == POSTPONED)
		output(line, is_odd_line);
}


/* Output notes with status UNDONE.
 * Called when argument -u is passed for the program.
 *
 * Set is_odd_line to 1 if the line to be outputted is odd.
 */
static void output_undone(char *line, int is_odd_line)
{
	if (get_note_status(line) == UNDONE)
		output(line, is_odd_line);
}


/* Functions outputs one note line without the date part */
static void output_without_date(char *line, int is_odd_line)
{
	char *token = strtok(line, "\t");

	if (token != NULL)
		printf("\t%s\t", token);
	else
		goto error;

	token = strtok(NULL, "\t");

	if (token != NULL)
		printf("%s\t", token);
	else
		goto error;

	token = strtok(NULL, "\t");
	token = strtok(NULL, "\t");

	if (token != NULL)
		output(token, is_odd_line);
	else
		goto error;

	return;

error:
	fail(stderr, "%s parsing line failed\n", __func__);

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

	if (lines == -2) {
		printf("Nothing to export.\n");
		return NULL;
	}

	fprintf(fp, "<!DOCTYPE html>\n");
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


/* Exports notes as CSV.
 *
 * id,status,date,content
 * 1,U,2013-11-11,some note
 *
 * Function returns the path to the CSV file or NULL on failure.
 */
static const char *export_csv(const char *path)
{
	FILE *fp = NULL;
	FILE *fpm = NULL;
	char *line = NULL;
	int lines = 0;

	fp = fopen(path, "w");

	if(!fp) {
		fail(stderr, "%s failed to open %s\n", __func__, path);
		return NULL;
	}

	fpm = get_memo_file_ptr("r");
	lines = count_file_lines(fpm);

	if(lines == -1) {
		fail(stderr, "%s: counting lines failed\n", __func__);
		return NULL;
	}

	/* Ignore empty file and return */
	if(lines == -2) {
		printf("Nothing to export.\n");
		return NULL;
	}

	fprintf(fp, "ID,Status,Date,Content\n");

	while (lines >= 0) {
		line = read_file_line(fpm);
		if (line) {
			/* Replace each occurence of tab character
			 * with a comma.
			 */
			for (int i = 0; i < strlen(line); i++) {
				if (line[i] == '\t')
					line[i] = ',';
			}

			fprintf(fp, "%s\n", line);
			free(line);
		}

		lines--;
	}

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
	int output_count = 0;

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
				if (current > start) {
					output_count++;
					output(line, is_odd(output_count));
				}
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
 *
 * Returns 0 on success and -1 on failure.
 */
static int delete_note(int id)
{
	return mark_note_status(DELETE, id);
}


/* Return the path to $HOME/.memorc.  On failure NULL is returned.
 * Caller is responsible for freeing the return value.
 */
static char *get_memo_conf_path()
{
	char *env = NULL;
	char *conf_path = NULL;
	size_t len = 0;

	env = getenv("XDG_CONFIG_HOME");

	if (env == NULL)
		env = getenv("HOME");

#ifdef _WIN32
	if (env == NULL)
		env = getenv("USERPROFILE");
#endif

	if (env == NULL){
		fail(stderr,"%s: getenv failed\n", __func__);
		return NULL;
	}

	/* +1 for \0 byte */
	len = strlen(env) + 1;

	/* +8 to have space for \"/.memorc\" */
	conf_path = malloc( (len + 8) * sizeof(char));

	if (conf_path == NULL) {
		fail(stderr, "%s: malloc failed\n", __func__);
		return NULL;
	}

	strcpy(conf_path, env);

#ifdef _WIN32
	strcat(conf_path, "/_memorc");
#else
	strcat(conf_path, "/.memorc");
#endif

	return conf_path;
}


/* ~/.memorc file format is following:
 *
 * PROPERTY=value
 *
 * e.g MEMO_PATH=/home/niko/.memo
 *
 * This function returns the value of the property. NULL is returned on
 * failure. On success, caller must free the return value.
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
				retval = malloc(len * sizeof(char));

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


/* Returns the default path. Default path is ~/.memo
 *
 * Caller must free the return value. On failure NULL is returned.
 */
static char *get_memo_default_path()
{
	char *path = NULL;
	size_t len = 0;
	char *env = getenv("HOME");
#ifdef _WIN32
	if (env == NULL)
		env = getenv("USERPROFILE");
#endif

	if (env == NULL){
		fail(stderr,"%s: getenv(\"HOME\") failed\n", __func__);
		return NULL;
	}

	/* +1 for \0 byte */
	len = strlen(env) + 1;

	/* +6 to have space for \"/.memo\" */
	path = malloc( (len + 6) * sizeof(char));

	if (path == NULL) {
		fail(stderr,"%s: malloc failed\n", __func__);
		return NULL;
	}

	strcpy(path, env);
#ifdef _WIN32
	strcat(path, "/_memo");
#else
	strcat(path, "/.memo");
#endif

	return path;
}


/* Function reads MEMO_PATH environment variable to see if it's set and
 * uses value from it as a path.  When MEMO_PATH is not set, function
 * reads $HOME/.memorc file. If the file is not found $HOME/.memo is
 * used as a fallback path.
 *
 * Returns the path to .memo file or NULL on failure.  Caller is
 * responsible for freeing the return value.
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
		path = malloc((strlen(env_path) + 1) * sizeof(char));

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

	if (!file_exists(conf_path)) {
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


/* Returns temporary .memo.tmp file.  It will be in the same directory
 * as the original .memo file.
 *
 * Returns NULL on failure.
 */
static char *get_temp_memo_path()
{
	char *orig = get_memo_file_path();

	if (orig == NULL)
		return NULL;

	char *tmp = malloc(sizeof(char) * (strlen(orig) + 5));

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


/* Remove new lines from the content.  Sometimes user might want to type
 * multiline note. This functions makes it one liner.
 */
static void remove_content_newlines(char *content)
{
	char *i = content;
	char *j = content;

	while (*j != '\0') {
		*i = *j++;
		if (*i != '\n')
			i++;
	}

	*i = '\0';
}


/* Replaces part of the note.
 * data is the new part defined by NotePart_t.
 *
 * Original note line is modified.
 *
 * Caller is responsible for freeing the return value.
 * Returns new note line on success, NULL on failure.
 */
static char *note_part_replace(NotePart_t part, char *note_line, const char *data)
{
	char *new_line = NULL;
	int size = ((strlen(note_line) + strlen(data)) + 1) * sizeof(char);

	new_line = malloc(size);

	if (new_line == NULL) {
		fail(stderr, "%s: malloc failed\n", __func__);
		return NULL;
	}

	char *token = NULL;

	/* Get the id and copy it */
	if ((token = strtok(note_line, "\t")) != NULL) {
		if (part == NOTE_ID) {
			/* Replace the original id with data */
			if (sprintf(new_line, "%s\t", data) < 0)
				goto error_clean_up;
		} else {
			/* Copy the original id */
			if (sprintf(new_line, "%s\t", token) < 0)
				goto error_clean_up;
		}
	} else {
		goto error_clean_up;
	}

	/* Get the status code and copy it */
	if ((token = strtok(NULL, "\t")) != NULL) {
		if (sprintf(new_line + strlen(new_line), "%s\t", token) < 0)
			goto error_clean_up;
	} else {
		goto error_clean_up;
	}

	/* Get the original date */
	if ( (token = strtok(NULL, "\t")) == NULL)
		goto error_clean_up;

	if (part == NOTE_DATE) {
		/* Copy data as the new date */
		if (sprintf(new_line + strlen(new_line), "%s\t", data) < 0)
			goto error_clean_up;

	} else {
		/* Copy the original date */
		if (sprintf(new_line + strlen(new_line), "%s\t", token) < 0)
			goto error_clean_up;
	}

	/* Get the original note content */
	if ( (token = strtok(NULL, "\t")) == NULL)
		goto error_clean_up;

	if (part == NOTE_CONTENT) {
		/* Copy the data as new content */
		if (sprintf(new_line + strlen(new_line), "%s", data) < 0)
			goto error_clean_up;
	} else {
		/* Copy the original note content, do not write \n because
		 * the token has it already.
		 */
		if (sprintf(new_line + strlen(new_line), "%s", token) < 0)
			goto error_clean_up;
	}

	return new_line;

error_clean_up:
	fail(stderr, "%s: replacing note data failed\n", __func__);
	free(new_line);

	return NULL;
}


/* Function replaces a note content with data.
 *
 * Data can be either a valid date or content.  Replace operation is
 * simply done by creating a temporary file, existing notes are written
 * line by line to it. Line that has matching id will be written with
 * new data. Then the original memo file is replaced with the temporary
 * one.
 *
 * Returns 0 on success, -1 on failure.
 */
static int replace_note(int id, const char *data)
{
	FILE *tmpfp = NULL;
	FILE *fp = NULL;
	char *memofile = NULL;
	char *tmpfile = NULL;
	int lines = 0;

	tmpfp = get_memo_tmpfile_ptr();

	if (tmpfp == NULL)
		return -1;

	fp = get_memo_file_ptr("r");

	lines = count_file_lines(fp);

	if (lines == -1) {
		fail(stderr, "%s: counting lines failed\n", __func__);
		fclose(tmpfp);

		return -1;
	}

	/* Empty file, ignore. */
	if (lines == -2) {
		fclose(fp);
		fclose(tmpfp);

		return -1;
	}

	memofile = get_memo_file_path();

	if (memofile == NULL) {
		fail(stderr, "%s failed to get memo file path\n", __func__);
		fclose(fp);
		fclose(tmpfp);

		return -1;
	}

	tmpfile = get_temp_memo_path();

	if (tmpfile == NULL) {
		fail(stderr, "%s failed to get memo tmp path\n", __func__);
		fclose(fp);
		fclose(tmpfp);

		free(memofile);

		return -1;
	}

	while (lines >= 0) {
		char *line = read_file_line(fp);

		if (line) {

			int curr_id = get_note_id_from_line(line);
			if (curr_id == id) {
				/* Found the note to be replaced
				 * Check if user wants to replace the date
				 * by validating the data as date. Otherwise
				 * assume content is being replaced.
				 */
				if (is_valid_date_format(data, 1) == 0) {
					char *new_line = NULL;
					new_line = note_part_replace(NOTE_DATE,
								line, data);
					if (new_line == NULL) {

						printf("Unable to replace note %d\n", id);

						free(memofile);
						free(tmpfile);
						fclose(fp);
						fclose(tmpfp);

						return -1;
					}

					fprintf(tmpfp, "%s\n", new_line);
					free(new_line);

				} else {
					char *new_line = NULL;
					new_line = note_part_replace(NOTE_CONTENT,
								line, data);

					if (new_line == NULL) {

						printf("Unable to replace note %d\n", id);

						free(memofile);
						free(tmpfile);
						fclose(fp);
						fclose(tmpfp);

						return -1;
					}

					fprintf(tmpfp, "%s\n", new_line);
					free(new_line);
				}
			} else {
				fprintf(tmpfp, "%s\n", line);
			}

			free(line);
		}

		lines--;
	}

	fclose(fp);
	fclose(tmpfp);

	if (file_exists(memofile))
		remove(memofile);

	rename(tmpfile, memofile);
	remove(tmpfile);

	free(memofile);
	free(tmpfile);

	return 0;
}


/* Simple helper function to convert integer to string.
 *
 * Returns the integer as string, on failure returns NULL.
 * Caller is responsible for freeing the return value.
 */
static char *integer_to_string(int id)
{
	char *buffer = NULL;

	/* Really, this should be enough space to hold our integer
	 * for the note id...
	 */
	buffer = malloc(15 * sizeof(char));

	if (buffer == NULL) {
		fail(stderr, "%s: malloc failed\n", __func__);
		return NULL;
	}

	if (snprintf(buffer, 15, "%d", id) < 0) {
		fail(stderr, "%s: convert failed\n", __func__);
		free(buffer);
		return NULL;
	}

	return buffer;
}


/* Function organizes note id codes.
 *
 * In Memo, if you have notes with id codes 1,2,3 and user deletes note
 * 2, remaining notes will be 1 and 3. This function organizes those
 * id codes so that the codes would be 1 and 2. The reason why Memo does
 * not do this automatically is because one might want to be able
 * to trust that the id codes never change (for example if Memo is used
 * as a part of some script)
 *
 * Returns 0 on success, -1 on failure.
 */
static int organize_note_identifiers()
{
	FILE *tmpfp = NULL;
	FILE *fp = NULL;
	char *memofile = NULL;
	char *tmpfile = NULL;
	int lines = 0;
	int id_counter = 1;

	tmpfp = get_memo_tmpfile_ptr();

	if (tmpfp == NULL)
		return -1;

	fp = get_memo_file_ptr("r");

	lines = count_file_lines(fp);

	if (lines == -1) {
		fail(stderr, "%s: counting lines failed\n", __func__);
		fclose(tmpfp);
		return -1;
	}

	/* An empty memo file, ignore and exit */
	if (lines == -2) {
		fclose(fp);
		fclose(tmpfp);
		return -1;
	}

	memofile = get_memo_file_path();

	if (memofile == NULL) {
		fail(stderr, "%s failed to get memo file path\n", __func__);
		fclose(fp);
		fclose(tmpfp);
		return -1;
	}

	tmpfile = get_temp_memo_path();

	if (tmpfile == NULL) {
		fail(stderr, "%s failed to get memo tmp path\n", __func__);
		fclose(fp);
		fclose(tmpfp);
		free(memofile);
		return -1;
	}

	while (lines >= 0) {

		char *line = read_file_line(fp);

		if (line) {

			/* Replace each note id with the value
			 * from id_counter which starts from one.
			 * id_counter is increased for every line.
			 */
			char *new_line = NULL;
			char *id = integer_to_string(id_counter);

			if (id == NULL) {
				fail(stderr, "%s: fatal error\n", __func__);
				free(memofile);
				free(tmpfile);
				fclose(fp);
				fclose(tmpfp);
				return -1;
			}

			new_line = note_part_replace(NOTE_ID, line, id);

			if (new_line == NULL) {
				fail(stderr, "%s: fatal error\n", __func__);
				free(memofile);
				free(tmpfile);
				fclose(fp);
				fclose(tmpfp);
				return -1;
			}

			fprintf(tmpfp, "%s\n", new_line);
			id_counter++;

			free(id);
			free(new_line);
			free(line);
		}

		lines--;
	}

	fclose(fp);
	fclose(tmpfp);

	if (file_exists(memofile))
		remove(memofile);

	rename(tmpfile, memofile);
	remove(tmpfile);

	free(memofile);
	free(tmpfile);

	return 0;
}


/* .memo file format is following:
 *
 * id     status     date           content
 * |      |          |              |
 * |- id  |-U/D/P    |- yyy-MM-dd   |- actual note
 *
 * sections are separated by a tab character
 *
 * Parameter date can be NULL. If date is given in valid
 * format(yyyy-MM-dd) it will be used for creating the note. If date is
 * NULL, current date will be used instead.
 *
 * Note will be marked with status "U" which means it's "undone".  "D"
 * means "done". With status P, note is marked as postponed.
 */
static int add_note(char *content, const char *date)
{
	FILE *fp = NULL;
	time_t t;
	struct tm *ti;
	int id = -1;
	char note_date[11];

	/* Do not add an empty note */
	if (strlen(content) == 0)
		return -1;

	remove_content_newlines(content);

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
    -a, --add <content> [yyyy-MM-dd]          Add a new note with optional date\n\
    -d, --delete  <id>                        Delete note by id\n\
    -D, --delete-all                          Delete all notes\n\
    -e, --export <format> <path>              Export notes a file\n\
                                              Format must be either csv or html\n\
    -f, --search <search>                     Find notes by search term\n\
    -F, --regex <regex>                       Find notes by regular expression\n\
    -i, --stdin                               Read from stdin until ^D\n\
    -l, --latest <n>                          Show latest n notes\n\
    -m, --set-done <id>                       Mark note status as done\n\
    -M, --set-undone <id>                     Mark note status as undone\n\
    -o, --list-date                           Show all notes organized by date\n\
    -O, --organize                            Reorder and organize note id codes\n\
    -p, --path                                Show current memo file path\n\
    -P, --postpone [id]                       Show postponed or mark note as postponed\n\
    -R, --delete-done                         Delete all notes marked as done\n\
    -r, --replace <id> [content]/[yyyy-MM-dd] Replace note content or date\n\
    -s, --list                                Show all notes except postponed\n\
                                              (Same as simply running memo)\n\
    -T, --set-done-all                        Mark all notes as done\n\
    -u, --list-undone                         Show only undone notes\n\
\n\
    -                                         Read from stdin\n\
    -h, --help                                Show short help and exit. This page\n\
    -V, --version                             Show version number of program\n\
\n\
For more information and examples see man memo(1).\n\
\n\
AUTHORS\n\
    Copyright (C) 2014-2019 Niko Rosvall <niko@byteptr.com>\n\
\n\
    Released under license GPL-3+. For more information, see\n\
    http://www.gnu.org/licenses\n\
"

	printf(HELP);
}


/* Function outputs currently used memo file path
 * to stdin.
 */
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


/* Function sorts the dates elements in the order of date.
 *
 * Make the *dates[] again.
 */
static void sort_dates_ascend(char *dates[], int date_index)
{
  int dates_int[date_index];

  /* Remove "-" from the dates elements and convert them into integer. */
  for (int i = 0; i < date_index; i++) {
		char *p = dates[i], *q = dates[i];
		while((*p = *q++)) p += (*p != '-');
		dates_int[i] = atoi(dates[i]);
  }

  /* Sort the dates_int elements in ascending order. */
  qsort((void *)dates_int , date_index , sizeof(dates_int[0]), int_sort);

  /* For each of dates_int elements,
   * make a string in valid date format ("yyyy-mm-dd") and reallocate it to dates.
   */
  for (int i = 0; i < date_index; i++) {
		dates[i][0] = '\0';

		/* Pick up the 4-digit year from dates_int[i]. */
		strcat(dates[i], integer_to_string(dates_int[i]/10000000));
		strcat(dates[i], integer_to_string((dates_int[i]/1000000) % 10));
		strcat(dates[i], integer_to_string((dates_int[i]/100000) % 10));
		strcat(dates[i], integer_to_string((dates_int[i]/10000) % 10));
		strcat(dates[i], "-");

		/* Pick up the 2-digit month from dates_int[i]. */
		strcat(dates[i], integer_to_string((dates_int[i]/1000) % 10));
		strcat(dates[i], integer_to_string((dates_int[i]/100) % 10));
		strcat(dates[i], "-");

		/* Pick up the 2-digit day from dates_int[i]. */
		strcat(dates[i], integer_to_string((dates_int[i]/10) % 10));
		strcat(dates[i], integer_to_string(dates_int[i] % 10));
  }
}


/* Comparator function needed for qsort function.
 *
 * Returns 1 if *a > *b, 0 if *a = *b, -1 if *a < *b.
 */
static int int_sort(const void *a, const void *b)
{
  if (*(int *)a < *(int *)b) {
		return -1;
  } else if (*(int *)a == *(int *)b) {
		return 0;
  }
  return 1;
}


/* Program entry point */
int main(int argc, char *argv[])
{
	char *path = NULL;
	int c;
	char *stdinline = NULL;
	int has_valid_options = 0;
	int organize_note_ids = 0;

	path = get_memo_file_path();

	if (path == NULL)
		return -1;

	if (!file_exists(path)) {
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

	/* This function is applied only if there's MARK_AS_DONE
	 * property available in ~/.memorc
	 */
	mark_old_as_done();

	if (argc == 1) {
		/* No arguments given, so just show notes */
		show_notes(-1);
	}

	static struct option long_options[] = {
		{"add", required_argument, 0, 'a'},
		{"delete", required_argument, 0, 'd'},
		{"delete-all", no_argument, 0, 'D'},
		{"export", required_argument, 0, 'e'},
		{"search", required_argument, 0, 'f'},
		{"regex", required_argument, 0, 'F'},
		{"stdin", no_argument, 0, 'i'},
		{"latest", required_argument, 0, 'l'},
		{"set-done", required_argument, 0, 'm'},
		{"set-undone", required_argument, 0, 'M'},
		{"by-date", no_argument, 0, 'o'},
		{"organize", no_argument, 0, 'O'},
		{"path", no_argument, 0, 'p'},
		{"postpone", no_argument, 0, 'P'},
		{"delete-done", no_argument, 0, 'R'},
		{"replace", required_argument, 0, 'r'},
		{"list", no_argument, 0, 's'},
		{"set-done-all", no_argument, 0, 'T'},
		{"list-undone", no_argument, 0, 'T'},
		{"help", no_argument, 0, 'h'},
		{"version", no_argument, 0, 'V'},
		{0, 0, 0, 0}
	};

	/* getopt_long stores the option index here. */
	int option_index = 0;

	while ((c = getopt_long(argc, argv, "a:d:De:f:F:hil:m:M:oOpPr:RsTuV", long_options, &option_index)) != -1){
		has_valid_options = 1;

		switch(c) {

		case 'a':
			if (argv[optind]) {
				if (is_valid_date_format(argv[optind], 0) == 0)
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
			if (argv[optind]) {
				if (strcmp(optarg, "csv") == 0) {
					export_csv(argv[optind]);
				} else if(strcmp(optarg, "html") == 0) {
					export_html(argv[optind]);
				}
			}
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
		case 'i':
			add_notes_from_stdin();
			break;
		case 'o':
			show_notes_tree();
			break;
		case 'O':
			organize_note_ids = 1;
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
		case 'r': {
			int id = atoi(optarg);
			if (argv[optind]) {
				replace_note(id, argv[optind]);
			}
			else {
				printf("Missing argument date or content, see -h\n");
				free(path);
				return 0;
			}
			break;
		}
		case 'R':
			mark_note_status(DELETE_DONE, -1);
			break;
		case 's':
			show_notes(-1);
			break;
		case 'T':
			mark_note_status(ALL_DONE, -1);
			break;
		case 'u':
			show_notes(UNDONE);
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
				printf("-l missing an argument <n>\n");
			else if (optopt == 'm')
				printf("-m missing an argument <id>\n");
			else if(optopt == 'M')
				printf("-M missing an argument <id>\n");
			else if(optopt == 'r')
				printf("-r missing an argument <id>\n");
			else
				printf("invalid option, see memo -h for help\n");
			break;
		}
	}

	if (organize_note_ids)
		organize_note_identifiers();

	/* Handle argument '-' to read line from stdin */
	if (argc > 1 && *argv[argc - 1] == '-' && strlen(argv[argc - 1]) == 1) {

		has_valid_options = 1;

		stdinline = read_file_line(stdin);

		if (stdinline) {
			add_note(stdinline, NULL);
			free(stdinline);
		}
	}

	if (argc > 1 && !has_valid_options)
		printf("invalid input, see memo -h for help\n");

	free(path);

	return 0;
}
