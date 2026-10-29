#ifndef __PLAYER2INPUT_H__
#define __PLAYER2INPUT_H__

#include <SDL.h>

namespace Sexy { class Graphics; }

// Second-player gamepad input (co-op groundwork).
// Step 1: detect a controller and drive a free cursor with the left stick.
// Board-side actions (plant, collect sun) come in later steps.
class Player2Input
{
public:
	static void		OnDeviceAdded(int theDeviceIndex);
	static void		OnDeviceRemoved(SDL_JoystickID theInstanceID);

	static bool		IsConnected();
	static void		Update();					// call once per Board::Update
	static void		Draw(Sexy::Graphics* g);			// call from Board::DrawUITop

	static int		GetX();						// board coordinates
	static int		GetY();

	// True only on the Update() in which the button went from released to pressed.
	static bool		JustPressed(SDL_GameControllerButton theButton);

	// L2 / R2 are analog axes on SDL; true only on the Update() where they pass the threshold.
	static bool		TriggerJustPressed(bool theLeft);

	static bool		IsDown(SDL_GameControllerButton theButton);

	// Cursor placement / limits (board coordinates).
	static void		SetPosition(int theX, int theY);
	static void		SetBounds(int theMinX, int theMinY, int theMaxX, int theMaxY);
};

#endif
