#ifndef __CRASHTRACE_H__
#define __CRASHTRACE_H__

// Tiny crash-diagnosis aid (Apple platforms): keeps a short ring of "breadcrumb" notes and the
// name of the activity currently running. If the app is killed by a fatal signal, they are
// written to Documents/pvz-crash.txt together with a raw backtrace, so a crash can be traced
// from the Files app. Does nothing on other platforms.
namespace CrashTrace
{
	void Install();									// once, at startup
	void Note(const char* theFormat, ...);			// add a breadcrumb
	void SetPhase(const char* thePhase);			// current activity (cheap)
	const char* GetPhase();

	// Sets a phase for the duration of a scope and restores the previous one afterwards.
	class Phase
	{
	public:
		explicit Phase(const char* thePhase) : mPrevious(GetPhase()) { SetPhase(thePhase); }
		~Phase() { SetPhase(mPrevious); }
	private:
		const char* mPrevious;
	};
}

#endif
