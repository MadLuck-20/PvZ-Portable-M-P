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
};

#endif
