/**
 * @brief about - Compatibility layer for about-dialog
 *
 * This utility provides a wrapper for executing the new 'about-dialog'
 * application using the argument format from the old 'about'.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2026 K. Lange
 */
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/utsname.h>

int main(int argc, char * argv[]) {
	if (argc < 2) {
		struct utsname u;
		uname(&u);
		*strchrnul(u.release, '-') = '\0';
		char *version_str;
		asprintf(&version_str, "ZRL %s", u.release);
		execvp("about-dialog",(char *const[]){
			"about-dialog",
			"--title-about", "ZRL",
			"--logo", "/usr/share/icon.ico",
			"--icon", "star",
			"--name", version_str,
			"--",
			"\xc2\xa9 2011-2026 K. Lange, et al.",
			"-",
			"ZRL \xe6\x98\xaf\xe5\x9f\xba\xe4\xba\x8e NCSA/\xe4\xbc\x8a\xe5\x88\xa9\xe8\xaf\xba\xe4\xbc\x8a\xe5\xa4\xa7\xe5\xad\xa6",
			"\xe8\xae\xb8\xe5\x8f\xaf\xe8\xaf\x81\xe5\x8f\x91\xe5\xb8\x83\xe7\x9a\x84\xe8\x87\xaa\xe7\x94\xb1\xe8\xbd\xaf\xe4\xbb\xb6\xe3\x80\x82",
			NULL
		});
		fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
	} else if (argc < 5) {
		fprintf(stderr, "Invalid arguments.\n");
		return 1;
	} else {
		char * args[100] = {
			"about-dialog",
			"--title",argv[1],
			"--logo",argv[2],
			"--icon","star",
			"--name",argv[3],
			NULL
		};
		int c = 9;

		if (argc > 6) {
			char * tmp;
			asprintf(&tmp, "%s,%s", argv[5], argv[6]);
			args[c++] = "--at";
			args[c++] = tmp;
		}

		args[c++] = "--";

		int i = 0;
		char * me = argv[4], * end;
		do {
			args[c++] = me;
			i++;
			end = strchr(me,'\n');
			if (end) {
				*end = '\0';
				me = end+1;
			}
			if (i >= 20) break;
		} while (end);

		return execvp("about-dialog",args);
	}
}
