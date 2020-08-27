/*	$OpenBSD: findfp.c,v 1.15 2013/12/17 16:33:27 deraadt Exp $ */
/*-
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Chris Torek.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <spawn.h>

struct POFILE
{
	FILE* fp;
	pid_t pid;
};

static POFILE* __popen_fail(int fds[2]) {
  close(fds[0]);
  close(fds[1]);
  return NULL;
}

static POFILE* and_popen(const char* cmd, const char* mode) {
  // Was the request for a socketpair or just a pipe?
  int fds[2];
  bool bidirectional = false;
  if (strchr(mode, '+') != NULL) {
    if (socketpair(AF_LOCAL, SOCK_CLOEXEC | SOCK_STREAM, 0, fds) == -1) return nullptr;
    bidirectional = true;
    mode = "r+";
  } else {
    if (pipe2(fds, O_CLOEXEC) == -1) return nullptr;
    mode = strrchr(mode, 'r') ? "r" : "w";
  }

  // If the parent wants to read, the child's fd needs to be stdout.
  int parent, child, desired_child_fd;
  if (*mode == 'r') {
    parent = 0;
    child = 1;
    desired_child_fd = STDOUT_FILENO;
  } else {
    parent = 1;
    child = 0;
    desired_child_fd = STDIN_FILENO;
  }

  // Ensure that the child fd isn't the desired child fd.
  if (fds[child] == desired_child_fd) {
    int new_fd = fcntl(fds[child], F_DUPFD_CLOEXEC, 0);
    if (new_fd == -1) return __popen_fail(fds);
    close(fds[child]);
    fds[child] = new_fd;
  }

  pid_t pid = vfork();
  if (pid == -1) return __popen_fail(fds);

  if (pid == 0) {
    close(fds[parent]);
    // POSIX says "The popen() function shall ensure that any streams from previous popen() calls
    // that remain open in the parent process are closed in the new child process."
    //_fwalk(__close_if_popened);
    // dup2 so that the child fd isn't closed on exec.
    if (dup2(fds[child], desired_child_fd) == -1) _exit(127);
    close(fds[child]);
    if (bidirectional) dup2(STDOUT_FILENO, STDIN_FILENO);
    execl("/bin/sh", "sh", "-c", cmd, nullptr);
    _exit(127);
  }

  FILE* fp = fdopen(fds[parent], mode);
  if (fp == nullptr) return __popen_fail(fds);

  close(fds[child]);

  POFILE* ret= new POFILE;
  ret->fp = fp;
  ret->pid = pid;

  return ret;
}

static int and_pclose(POFILE* pofp) {
  int r = fclose(pofp->fp);

  // If we were created by popen(3), wait for the child.
  pid_t pid = pofp->pid;
  if (pid > 0) {
    int status;
    if (wait4(pid, &status, 0, nullptr) != -1) {
      r = status;
    }
  }
  pofp->pid=0;

  delete pofp;

  return r;
}

static int and_system(const char* command) {
  // "The system() function shall always return non-zero when command is NULL."
  // http://pubs.opengroup.org/onlinepubs/9699919799/functions/system.html
  if (command == nullptr) return 1;

  /*ScopedSignalBlocker sigchld_blocker(SIGCHLD);
  ScopedSignalHandler sigint_ignorer(SIGINT, SIG_IGN);
  ScopedSignalHandler sigquit_ignorer(SIGQUIT, SIG_IGN);

  sigset64_t default_mask = {};
  if (sigint_ignorer.old_action_.sa_handler != SIG_IGN) sigaddset64(&default_mask, SIGINT);
  if (sigquit_ignorer.old_action_.sa_handler != SIG_IGN) sigaddset64(&default_mask, SIGQUIT);*/

  static constexpr int flags = POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK;
  posix_spawnattr_t attributes;
  if ((errno = posix_spawnattr_init(&attributes))) return -1;
  //if ((errno = posix_spawnattr_setsigdefault64(&attributes, &default_mask))) return -1;
  //if ((errno = posix_spawnattr_setsigmask64(&attributes, &sigchld_blocker.old_set_))) return -1;
  if ((errno = posix_spawnattr_setflags(&attributes, flags))) return -1;

  const char* argv[] = { "sh", "-c", command, nullptr };
  pid_t child;
  if ((errno = posix_spawn(&child, "/bin/sh", nullptr, &attributes,
                           const_cast<char**>(argv), environ)) != 0) {
    return -1;
  }

  posix_spawnattr_destroy(&attributes);

  int status;
  pid_t pid = TEMP_FAILURE_RETRY(waitpid(child, &status, 0));
  return (pid == -1 ? -1 : status);
}
