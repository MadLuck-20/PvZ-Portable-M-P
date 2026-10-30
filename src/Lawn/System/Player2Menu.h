#ifndef __PLAYER2MENU_H__
#define __PLAYER2MENU_H__

class LawnApp;
namespace Sexy { class Graphics; }

// Gamepad (player 2) control of the pause menu and the dialogs opened from it.
//
// Whoever opens the pause menu owns it: while player 2 owns it, touch/mouse input is
// ignored; while player 1 owns it, the gamepad is ignored. A dialog that nobody opened
// on purpose (e.g. an end-of-level dialog) is taken by whoever acts on it first.
class Player2Menu
{
public:
	static void		Update(LawnApp* theApp);			// once per update tick
	static void		OnOptionsOpened();					// called by LawnApp::DoNewOptions
	static bool		BlocksPointerInput();				// true while player 2 owns the menu
	static void		NotifyPointerDown();				// player 1 touched/clicked while a dialog is open
	static bool		IsActive();							// player 2 owns an open menu
	static void		Draw(Sexy::Graphics* g);			// virtual cursor and slider hint
};

#endif
