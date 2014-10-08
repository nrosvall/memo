#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Function declarations */
char *read_memo_line(FILE *fp);
char *get_memo_file_path();
char *get_temp_memo_path();
int add_note(const char *content, const char *tags);
int get_next_id();
int delete_note(int id);
int show_notes();
int search_notes(char *search);
void show_latest(int count);
FILE *get_memo_file_ptr();
/* Function declarations end */

FILE *get_memo_file_ptr(char *mode) 
{
	FILE *fp = NULL;
	char *path = get_memo_file_path();

	if(path == NULL)
		return NULL;
	
	fp = fopen(path, mode);

	if(fp == NULL)
		return NULL;

	free(path);

	return fp;
}

/* Read a line from the file.
 * Return NULL on failure.
 * Caller is responsible for freeing the return value
 */
char *read_memo_line(FILE *fp)
{
	if(fp == NULL)
		return NULL;

	int length = 128;
	char *buffer = (char*)malloc(sizeof(char) * length);
	
	if(buffer == NULL)
		return NULL;

	int count = 0;

	char ch = getc(fp);

       /* Read char by char until the end of the line.
        * and allocate memory as needed.
        */
	while((ch != '\n') && (ch != EOF)) {
		if(count == length){
			length += 128;
			buffer = realloc(buffer, length);
			if(buffer == NULL)
				return NULL;
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
	char *last_line = NULL;

	fp = get_memo_file_ptr("r");

	if(fp == NULL)
		return -1;

	while(!feof(fp)) {
		last_line = read_memo_line(fp);
		if(last_line != NULL) {
			if(strcmp(last_line, "") != 0) {
				char *endptr;
				id = strtol(last_line, &endptr, 10);
			}
		}
	}

	if(last_line != NULL)
		free(last_line);

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

	fp = get_memo_file_ptr("r");

	if(fp == NULL)
		return -1;

	while(!feof(fp)) {
		line = read_memo_line(fp);
		if(line != NULL && strcmp(line, "") != 0) {
			printf("%s\n", line);
			count++;
		}
	}

	fclose(fp);

	return count;
}

int search_notes(char *search)
{

	return 0;
}

/* Show latest n notes */
void show_latest(int n)
{
	FILE *fp = NULL;
	char *line;
	int lines = 0;
	int ch = 0;
	int start;
	int current = 0;

	fp = get_memo_file_ptr("r");

	if(fp != NULL) {
		/* Count lines by new line characters */
		while(!feof(fp)) {
			ch = fgetc(fp);
			if(ch == '\n')
				lines++;
		}
		/* Go to beginning of the file */
		rewind(fp);

		if(n > lines)
			start = lines;
		else
			start = lines - n;

		while(!feof(fp)) {
			line = read_memo_line(fp);
			if(line != NULL && strcmp(line,"") != 0) {
				if(current >= start)
					printf("%s\n",line);
				current++;
			}
		}

		fclose(fp);
	}
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

	tmp = get_temp_memo_path();

	if(tmp == NULL)
		return -1;

	char *memofile = get_memo_file_path();

	if(memofile == NULL)
		return -1;

	tmpfp = fopen(tmp, "w");

	if(tmpfp == NULL) {
		free(memofile);
		return -1;
	}

	fp = get_memo_file_ptr("r");

	if(fp == NULL) {
		free(memofile);
		return -1;
	}

	while(!feof(fp)) {
		line = read_memo_line(fp);
		if(line != NULL && strcmp(line, "") != 0) {
			char *endptr;
			int curr = strtol(line, &endptr, 10);
			if(curr != id)
				fprintf(tmpfp, "%s\n", line);
		}
	}
	
	fclose(fp);
	fclose(tmpfp);

	if(access(memofile, F_OK) == 0)
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

	if(path == NULL)
		return NULL;

	if(env == NULL)
		return NULL;
	
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

	if(orig == NULL)
		return NULL;

	char *tmp = (char*)malloc(sizeof(char) * (strlen(orig) + 5));

	if(tmp == NULL)
		return NULL;

	strcpy(tmp, orig);
	strcat(tmp, ".tmp");

	return tmp;
}

/*
 * .memo file format is following:
 *
 * id     date           content         tags
 * |      |              |               |
 * |- id  |- yyy-MM-dd   |- actual note  |- tags, semicolon separated
 *
 * sections are separated by a tab
 *
 */
int add_note(const char *content, const char *tags)
{
	FILE *fp = NULL;
	time_t t;
	struct tm *ti;
	int id = -1;
	char date[11];


	fp = get_memo_file_ptr("a");

	if(fp == NULL){
		printf("Error opening ~/.memo\n");
		return -1;
	}

	time(&t);
	ti = localtime(&t);
	
	id = get_next_id();

	if(id == -1){
		printf("Problem getting an id for the note.\n");
	} else {
		strftime(date, 80, "%Y-%m-%d", ti);
		fprintf(fp, "%d\t%s\t%s\t%s\n", id, date, content, tags);
	}

	fclose(fp);

	return id;
}



int main(int argc, char *argv[])
{
	add_note("Hello from Memo","@sometag");
	
	//delete_note(2);

	//int count = show_notes();
	//printf("%d notes found:\n", count);
	//show_latest(4);

	return 0;
}
