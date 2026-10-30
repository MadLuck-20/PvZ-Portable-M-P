#include "CrashTrace.h"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__APPLE__)
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace CrashTrace
{
	static constexpr int kRingSize = 40;
	static constexpr int kLineSize = 120;

	static char					gRing[kRingSize][kLineSize];
	static volatile unsigned	gNext = 0;
	static const char* volatile	gPhase = "idle";

	void SetPhase(const char* thePhase) { gPhase = thePhase; }
	const char* GetPhase() { return gPhase; }

	void Note(const char* theFormat, ...)
	{
		char* aSlot = gRing[gNext % kRingSize];
		gNext = gNext + 1;

		va_list aArgs;
		va_start(aArgs, theFormat);
		vsnprintf(aSlot, kLineSize, theFormat, aArgs);
		va_end(aArgs);
	}

#if defined(__APPLE__)
	static char					gPath[600] = {};
	static struct sigaction		gOld[32];
	static volatile bool		gInHandler = false;
	static const int			kSignals[] = { SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE, SIGTRAP };

	static void WriteStr(int theFd, const char* theText)
	{
		const size_t aLen = strlen(theText);
		if (aLen > 0)
			(void)!write(theFd, theText, aLen);
	}

	static void Handler(int theSig, siginfo_t* theInfo, void* theContext)
	{
		if (!gInHandler)
		{
			gInHandler = true;

			const int aFd = open(gPath, O_WRONLY | O_CREAT | O_APPEND, 0644);
			if (aFd >= 0)
			{
				char aHeader[200];
				snprintf(aHeader, sizeof(aHeader), "\n=== crash: signal %d, address %p, phase '%s' ===\nlast notes (oldest first):\n",
					theSig, theInfo != nullptr ? theInfo->si_addr : nullptr, gPhase);
				WriteStr(aFd, aHeader);

				const unsigned aEnd = gNext;
				const unsigned aStart = aEnd > (unsigned)kRingSize ? aEnd - kRingSize : 0;
				for (unsigned i = aStart; i < aEnd; i++)
				{
					WriteStr(aFd, "  ");
					WriteStr(aFd, gRing[i % kRingSize]);
					WriteStr(aFd, "\n");
				}

				WriteStr(aFd, "backtrace:\n");
				void* aFrames[64];
				const int aCount = backtrace(aFrames, 64);
				backtrace_symbols_fd(aFrames, aCount, aFd);
				close(aFd);
			}
		}

		// Hand the signal on to whoever was registered before us (e.g. the host app).
		const struct sigaction& aOld = gOld[theSig];
		if ((aOld.sa_flags & SA_SIGINFO) && aOld.sa_sigaction != nullptr)
			aOld.sa_sigaction(theSig, theInfo, theContext);
		else if (!(aOld.sa_flags & SA_SIGINFO) && aOld.sa_handler != SIG_DFL && aOld.sa_handler != SIG_IGN && aOld.sa_handler != nullptr)
			aOld.sa_handler(theSig);
		else
		{
			signal(theSig, SIG_DFL);
			raise(theSig);
		}
	}

	void Install()
	{
		static bool sInstalled = false;
		if (sInstalled)
			return;
		sInstalled = true;

		const char* aHome = getenv("HOME");
		if (aHome == nullptr)
			return;
		snprintf(gPath, sizeof(gPath), "%s/Documents/pvz-crash.txt", aHome);

		for (const int aSig : kSignals)
		{
			struct sigaction aAction;
			memset(&aAction, 0, sizeof(aAction));
			aAction.sa_sigaction = Handler;
			aAction.sa_flags = SA_SIGINFO;
			sigemptyset(&aAction.sa_mask);
			sigaction(aSig, &aAction, &gOld[aSig]);
		}
	}
#else
	void Install() {}
#endif
}
