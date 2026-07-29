/**
 * @brief live-session - Run live CD user session.
 *
 * Launches the general session manager as 'local', waits for the
 * session to end, then launches the login manager.
 *
 * @copyright
 * This file is part of ToaruOS and is released under the terms
 * of the NCSA / University of Illinois License - see LICENSE.md
 * Copyright (C) 2018 K. Lange
 */
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <toaru/auth.h>
#include <toaru/yutani.h>
#include <toaru/trace.h>
#define TRACE_APP_NAME "live-session"

int main(int argc, char * argv[]) {
	int pid;

	if (geteuid() != 0) {
		return 1;
	}

	/*
	 * Hold a yutani connection in the parent so the compositor does not
	 * terminate itself when the session it started first exits.
	 */
	yutani_init();

	/*
	 * First-boot setup (OOBE):
	 * If the out-of-box experience has not been completed yet, run it
	 * before presenting the login manager. This ensures a fresh install
	 * goes through the setup wizard instead of silently dropping into the
	 * 'local' account.
	 */
	if (access("/etc/oobe_complete", F_OK) != 0) {
		TRACE("OOBE not completed, launching setup wizard...");
		int _oobe_pid = fork();
		if (!_oobe_pid) {
			char * args[] = {"/bin/oobe", NULL};
			execvp(args[0], args);
			TRACE("Failed to start OOBE!");
			exit(1);
		}
		int _status;
		waitpid(_oobe_pid, &_status, 0);
		TRACE("OOBE completed, launching graphical login.");
	}

	/*
	 * Hand off to the graphical login manager. We intentionally do NOT
	 * auto-log-in as 'local' here: the user should log in with the account
	 * they created during OOBE (or any other valid account).
	 */
	TRACE("Launching graphical login.");
	int _glogin_pid = fork();
	if (!_glogin_pid) {
		char * args[] = {"/bin/glogin", NULL};
		execvp(args[0], args);
		system("reboot");
		exit(127);
	}

	do {
		pid = wait(NULL);
	} while ((pid > 0 && pid != _glogin_pid) || (pid == -1 && errno == EINTR));

	return 0;
}
