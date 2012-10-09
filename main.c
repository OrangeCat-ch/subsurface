/* main.c */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "dive.h"
#include "divelist.h"

#ifdef DEBUGFILE
char *debugfilename;
FILE *debugfile;
#endif

struct units output_units;

/* random helper functions, used here or elsewhere */
static int sortfn(const void *_a, const void *_b)
{
	const struct dive *a = *(void **)_a;
	const struct dive *b = *(void **)_b;

	if (a->when < b->when)
		return -1;
	if (a->when > b->when)
		return 1;
	return 0;
}

const char *weekday(int wday)
{
	static const char wday_array[7][4] = {
		"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
	};
	return wday_array[wday];
}

const char *monthname(int mon)
{
	static const char month_array[12][4] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
	};
	return month_array[mon];
}

/*
 * When adding dives to the dive table, we try to renumber
 * the new dives based on any old dives in the dive table.
 *
 * But we only do it if:
 *
 *  - the last dive in the old dive table was numbered
 *
 *  - all the new dives are strictly at the end (so the
 *    "last dive" is at the same location in the dive table
 *    after re-sorting the dives.
 *
 *  - none of the new dives have any numbers
 *
 * This catches the common case of importing new dives from
 * a dive computer, and gives them proper numbers based on
 * your old dive list. But it tries to be very conservative
 * and not give numbers if there is *any* question about
 * what the numbers should be - in which case you need to do
 * a manual re-numbering.
 */
static void try_to_renumber(struct dive *last, int preexisting)
{
	int i, nr;

	/*
	 * If the new dives aren't all strictly at the end,
	 * we're going to expect the user to do a manual
	 * renumbering.
	 */
	if (get_dive(preexisting-1) != last)
		return;

	/*
	 * If any of the new dives already had a number,
	 * we'll have to do a manual renumbering.
	 */
	for (i = preexisting; i < dive_table.nr; i++) {
		struct dive *dive = get_dive(i);
		if (dive->number)
			return;
	}

	/*
	 * Ok, renumber..
	 */
	nr = last->number;
	for (i = preexisting; i < dive_table.nr; i++) {
		struct dive *dive = get_dive(i);
		dive->number = ++nr;
	}
}

/*
 * track whether we switched to importing dives
 */
static gboolean imported = FALSE;

/*
 * This doesn't really report anything at all. We just sort the
 * dives, the GUI does the reporting
 */
void report_dives(gboolean is_imported)
{
	int i;
	int preexisting = dive_table.preexisting;
	struct dive *last;

	/* This does the right thing for -1: NULL */
	last = get_dive(preexisting-1);

	qsort(dive_table.dives, dive_table.nr, sizeof(struct dive *), sortfn);

	for (i = 1; i < dive_table.nr; i++) {
		struct dive **pp = &dive_table.dives[i-1];
		struct dive *prev = pp[0];
		struct dive *dive = pp[1];
		struct dive *merged;

		if (prev->when + prev->duration.seconds < dive->when)
			continue;

		merged = try_to_merge(prev, dive);
		if (!merged)
			continue;

		/* careful - we might free the dive that last points to. Oops... */
		if (last == prev || last == dive)
			last = merged;
		free(prev);
		free(dive);
		*pp = merged;
		dive_table.nr--;
		memmove(pp+1, pp+2, sizeof(*pp)*(dive_table.nr - i));

		/* Redo the new 'i'th dive */
		i--;
	}

	if (is_imported) {
		/* Was the previous dive table state numbered? */
		if (last && last->number)
			try_to_renumber(last, preexisting);

		/* did we add dives to the dive table? */
		if (preexisting != dive_table.nr)
			mark_divelist_changed(TRUE);
	}
	dive_table.preexisting = dive_table.nr;
	dive_list_update_dives();
}

static void parse_argument(const char *arg)
{
	const char *p = arg+1;

	do {
		switch (*p) {
		case 'v':
			verbose++;
			continue;
		case '-':
			/* long options with -- */
			if (strcmp(arg,"--import") == 0) {
				/* mark the dives so far as the base,
				 * everything after is imported */
				report_dives(FALSE);
				imported = TRUE;
				return;
			}
			/* fallthrough */
		case 'p':
			/* ignore process serial number argument when run as native macosx app */
			if (strncmp(arg, "-psn_", 5) == 0) {
				return;
			}
			/* fallthrough */
		default:
			fprintf(stderr, "Bad argument '%s'\n", arg);
			exit(1);
		}
	} while (*++p);
}

void update_dive(struct dive *new_dive)
{
	static struct dive *buffered_dive;
	struct dive *old_dive = buffered_dive;

	if (old_dive) {
		flush_divelist(old_dive);
	}
	if (new_dive) {
		show_dive_info(new_dive);
		show_dive_equipment(new_dive, W_IDX_PRIMARY);
		show_dive_stats(new_dive);
	}
	buffered_dive = new_dive;
}

void renumber_dives(int nr)
{
	int i;

	for (i = 0; i < dive_table.nr; i++) {
		struct dive *dive = dive_table.dives[i];
		dive->number = nr + i;
		flush_divelist(dive);
	}
	mark_divelist_changed(TRUE);
}

int main(int argc, char **argv)
{
	int i;
	gboolean no_filenames = TRUE;

	output_units = SI_units;

	subsurface_command_line_init(&argc, &argv);
	parse_xml_init();

	init_ui(&argc, &argv);

#ifdef DEBUGFILE
	debugfilename = (char *)subsurface_default_filename();
	strncpy(debugfilename + strlen(debugfilename) - 3, "log", 3);
	if (g_mkdir_with_parents(g_path_get_dirname(debugfilename), 0664) != 0 ||
	    (debugfile = g_fopen(debugfilename, "w")) == NULL)
		printf("oh boy, can't create debugfile");
#endif
	for (i = 1; i < argc; i++) {
		const char *a = argv[i];

		if (a[0] == '-') {
			parse_argument(a);
			continue;
		}
		no_filenames = FALSE;
		GError *error = NULL;
		parse_file(a, &error, TRUE);

		if (error != NULL)
		{
			report_error(error);
			g_error_free(error);
			error = NULL;
		}
	}
	if (no_filenames) {
		GError *error = NULL;
		const char *filename = subsurface_default_filename();
		parse_file(filename, &error, TRUE);
		/* don't report errors - this file may not exist, but make
		   sure we remember this as the filename in use */
		set_filename(filename, FALSE);
		free((void *)filename);
	}
	report_dives(imported);
	if (dive_table.nr == 0)
		show_dive_info(NULL);
	run_ui();
	exit_ui();

	parse_xml_exit();
	subsurface_command_line_exit(&argc, &argv);

#ifdef DEBUGFILE
	if (debugfile)
		fclose(debugfile);
#endif
	return 0;
}
