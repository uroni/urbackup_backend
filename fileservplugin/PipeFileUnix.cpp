/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011-2015 Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU Affero General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "PipeFile.h"
#include "../stringtools.h"
#include "../Interface/Server.h"
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <stdlib.h>
#include "../config.h"
#if defined(HAVE_SPAWN_H)
#include <spawn.h>
#endif
#include <sys/stat.h>
#include <fcntl.h>

#if defined(O_CLOEXEC) && !defined(__APPLE__) && !defined(__FreeBSD__)
#define clopipe(x) pipe2(x, O_CLOEXEC)
#else
int clopipe(int* x)
{
        int rc = pipe(x);
        if(rc!=0) return rc;
        fcntl(x[0], F_SETFD, FD_CLOEXEC);
        fcntl(x[1], F_SETFD, FD_CLOEXEC);
        return rc;
}
#endif

PipeFile::PipeFile(const std::string& pCmd)
	: PipeFileBase(pCmd),
		hStderr(0),
		hStdout(0)
{
	int stdout_pipe[2];

	if(clopipe(stdout_pipe) == -1)
	{
		Server->Log("Error creating stdout pipe: " + convert(errno), LL_ERROR);
		has_error=true;
		return;
	}

	int stderr_pipe[2];
	if(clopipe(stderr_pipe) == -1)
	{
		Server->Log("Error creating stderr pipe: " + convert(errno), LL_ERROR);
		has_error=true;
		return;
	}
	
#if defined(HAVE_SPAWN_H)
	posix_spawn_file_actions_t fa;
	if(posix_spawn_file_actions_init(&fa)!=0)
	{
		Server->Log("Error posix_spawn_file_actions_init: " + convert(errno), LL_ERROR);
		has_error=true;
		return;
	}
	
	if(posix_spawn_file_actions_adddup2(&fa, stdout_pipe[1], STDOUT_FILENO)!=0)
	{
		Server->Log("Error posix_spawn_file_actions_adddup2(1): " + convert(errno), LL_ERROR);
		has_error=true;
		return;
	}
	
	if(posix_spawn_file_actions_adddup2(&fa, stderr_pipe[1], STDERR_FILENO)!=0)
	{
		Server->Log("Error posix_spawn_file_actions_adddup2(2): " + convert(errno), LL_ERROR);
		has_error=true;
		return;
	}
	
	const char* argv[] = {"sh", "-c", &pCmd[0], NULL};
	const char* envp[] = {NULL};
	if(posix_spawn(&child_pid, "/bin/sh", &fa, NULL, const_cast<char**>(argv), const_cast<char**>(envp))!=0)
	{
		Server->Log("Error executing command: " + convert(errno), LL_ERROR);
		has_error=true;
		return;
	}
	
	close(stdout_pipe[1]);
	close(stderr_pipe[1]);

	hStdout = stdout_pipe[0];
	hStderr = stderr_pipe[0];

	init();	
#else
	child_pid = fork();

	if(child_pid==-1)
	{
		Server->Log("Error forking to execute command: " + convert(errno), LL_ERROR);
		has_error=true;
		return;
	}
	else if(child_pid==0)
	{
		while ((dup2(stdout_pipe[1], STDOUT_FILENO) == -1) && (errno == EINTR)) {}
		while ((dup2(stderr_pipe[1], STDERR_FILENO) == -1) && (errno == EINTR)) {}

		close(stdout_pipe[0]);
		close(stdout_pipe[1]);
		close(stderr_pipe[0]);
		close(stderr_pipe[1]);

		int rc = system(pCmd.c_str());
		
		if(rc>127)
		{
			rc = 127;
		}
		
		_exit(rc);
	}
	else
	{
		close(stdout_pipe[1]);
		close(stderr_pipe[1]);

		hStdout = stdout_pipe[0];
		hStderr = stderr_pipe[0];

		init();
	}
#endif
}

PipeFile::~PipeFile()
{
	
}

void PipeFile::forceExitWait()
{
	if(child_pid!=-1)
	{
		kill(child_pid, SIGKILL);
		int status;
		waitpid(child_pid, &status, 0);
	}

	close(hStdout);
	close(hStderr);
	
	waitForExit();
}

bool PipeFile::readIntoBuffer(int hStd, char* buf, size_t buf_avail, size_t& read_bytes)
{
	ssize_t rc = read(hStd, buf, buf_avail);

	if(rc==0 && errno==EINTR)
	{
		return readIntoBuffer(hStd, buf, buf_avail, read_bytes);
	}
	else if(rc<=0)
	{
		return false;
	}
	else
	{
		read_bytes = static_cast<size_t>(rc);
		return true;
	}
}

bool PipeFile::readStdoutIntoBuffer(char* buf, size_t buf_avail, size_t& read_bytes)
{
	return readIntoBuffer(hStdout, buf, buf_avail, read_bytes);
}

bool PipeFile::readStderrIntoBuffer(char* buf, size_t buf_avail, size_t& read_bytes)
{
	return readIntoBuffer(hStderr, buf, buf_avail, read_bytes);
}

bool PipeFile::getExitCode(int& exit_code)
{
	int status;
	int rc = waitpid(child_pid, &status, WNOHANG);

	if(rc==-1)
	{
		Server->Log("Error while waiting for exit code: " + convert(errno), LL_ERROR);
		return false;
	}
	else if(rc==0)
	{
		Server->Log("Process is still active", LL_ERROR);
		return false;
	}
	else
	{
		child_pid=-1;
		
		if(WIFEXITED(status))
		{
			exit_code = WEXITSTATUS(status);
			return true;
		}
		else if(WIFSIGNALED(status))
		{
			Server->Log("Script was terminated by signal " + convert(WTERMSIG(status)), LL_ERROR);
			return false;
		}
		else if(WCOREDUMP(status))
		{
			Server->Log("Script crashed", LL_ERROR);
			return false;
		}
		else if(WIFSTOPPED(status))
		{
			Server->Log("Script was stopped by signal " + convert(WSTOPSIG(status)), LL_ERROR);
			return false;
		}
		else
		{
			Server->Log("Unknown process status change", LL_ERROR);
			return false;
		}
	}
}
