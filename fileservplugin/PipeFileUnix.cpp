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

PipeFile::PipeFile(const std::wstring& pCmd)
	: PipeFileBase(pCmd),
		hStderr(0),
		hStdout(0)
{
	int stdout_pipe[2];

	if(pipe(stdout_pipe) == -1)
	{
		Server->Log("Error creating stdout pipe: " + nconvert(errno), LL_ERROR);
		has_error=true;
		return;
	}

	int stderr_pipe[2];
	if(pipe(stderr_pipe) == -1)
	{
		Server->Log("Error creating stderr pipe: " + nconvert(errno), LL_ERROR);
		has_error=true;
		return;
	}

	child_pid = fork();

	if(child_pid==-1)
	{
		Server->Log("Error forking to execute command: " + nconvert(errno), LL_ERROR);
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

		int rc = system(Server->ConvertToUTF8(pCmd).c_str());

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
	int rc = waitpid(child_pid, &status, 0);

	if(rc==-1)
	{
		Server->Log("Error while waiting for exit code: " + nconvert(errno), LL_ERROR);
		return false;
	}
	else
	{
		if(WIFEXITED(status))
		{
			exit_code = WEXITSTATUS(status);
			return true;
		}
		else if(WIFSIGNALED(status))
		{
			Server->Log("Script was terminated by signal " + nconvert(WTERMSIG(status)), LL_ERROR);
			return false;
		}
		else if(WCOREDUMP(status))
		{
			Server->Log("Script crashed", LL_ERROR);
			return false;
		}
		else if(WIFSTOPPED(status))
		{
			Server->Log("Script was stopped by signal " + nconvert(WSTOPSIG(status)), LL_ERROR);
			return false;
		}
		else
		{
			Server->Log("Unknown process status change", LL_ERROR);
			return false;
		}
	}
}
