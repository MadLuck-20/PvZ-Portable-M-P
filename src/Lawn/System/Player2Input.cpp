#include "Player2Input.h"
#include "../../GameConstants.h"
#include "../../SexyAppFramework/graphics/Graphics.h"
#include <algorithm>
#include <cmath>

using namespace Sexy;

static SDL_GameController*	gController = nullptr;
static float				gCursorX = BOARD_WIDTH / 2.0f;
static float				gCursorY = BOARD_HEIGHT / 2.0f;

static bool					gButtonPrev[SDL_CONTROLLER_BUTTON_MAX] = {};
static bool					gButtonCurr[SDL_CONTROLLER_BUTTON_MAX] = {};

static int					gScanCountdown = 0;
static bool					gTriggerPrev[2] = {};
static bool					gTriggerCurr[2] = {};

static constexpr float		kDeadZone = 0.20f;
static constexpr float		kSpeedPixelsPerUpdate = 7.0f;

static float NormalizeAxis(Sint16 theRaw)
{
	float aValue = theRaw / 32767.0f;
	if (std::fabs(aValue) < kDeadZone)
		return 0.0f;
	return std::clamp(aValue, -1.0f, 1.0f);
}

void Player2Input::OnDeviceAdded(int theDeviceIndex)
{
	if (gController != nullptr)
		return; // only one P2 pad for now
	if (!SDL_IsGameController(theDeviceIndex))
		return;
	gController = SDL_GameControllerOpen(theDeviceIndex);
}

void Player2Input::OnDeviceRemoved(SDL_JoystickID theInstanceID)
{
	if (gController == nullptr)
		return;
	SDL_Joystick* aJoy = SDL_GameControllerGetJoystick(gController);
	if (aJoy != nullptr && SDL_JoystickInstanceID(aJoy) == theInstanceID)
	{
		SDL_GameControllerClose(gController);
		gController = nullptr;
	}
}

bool Player2Input::IsConnected()
{
	return gController != nullptr;
}

void Player2Input::Update()
{
	for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++)
	{
		gButtonPrev[i] = gButtonCurr[i];
		gButtonCurr[i] = (gController != nullptr) &&
			SDL_GameControllerGetButton(gController, (SDL_GameControllerButton)i) != 0;
	}

	gTriggerPrev[0] = gTriggerCurr[0];
	gTriggerPrev[1] = gTriggerCurr[1];
	gTriggerCurr[0] = (gController != nullptr) &&
		SDL_GameControllerGetAxis(gController, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000;
	gTriggerCurr[1] = (gController != nullptr) &&
		SDL_GameControllerGetAxis(gController, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000;

	if (gController == nullptr)
	{
		// A pad that was already connected when the app started never produced a
		// "device added" event we could see, so also look for one every ~0.3 s.
		if (--gScanCountdown <= 0)
		{
			gScanCountdown = 30;
			const int aNum = SDL_NumJoysticks();
			for (int i = 0; i < aNum && gController == nullptr; i++)
				OnDeviceAdded(i);
		}
		return;
	}

	float aDX = NormalizeAxis(SDL_GameControllerGetAxis(gController, SDL_CONTROLLER_AXIS_LEFTX));
	float aDY = NormalizeAxis(SDL_GameControllerGetAxis(gController, SDL_CONTROLLER_AXIS_LEFTY));
	gCursorX = std::clamp(gCursorX + aDX * kSpeedPixelsPerUpdate, 0.0f, (float)BOARD_WIDTH);
	gCursorY = std::clamp(gCursorY + aDY * kSpeedPixelsPerUpdate, 0.0f, (float)BOARD_HEIGHT);
}

bool Player2Input::JustPressed(SDL_GameControllerButton theButton)
{
	if (theButton < 0 || theButton >= SDL_CONTROLLER_BUTTON_MAX)
		return false;
	return gButtonCurr[theButton] && !gButtonPrev[theButton];
}

bool Player2Input::TriggerJustPressed(bool theLeft)
{
	const int i = theLeft ? 0 : 1;
	return gTriggerCurr[i] && !gTriggerPrev[i];
}

int Player2Input::GetX() { return (int)gCursorX; }
int Player2Input::GetY() { return (int)gCursorY; }

void Player2Input::Draw(Sexy::Graphics* g)
{
	if (gController == nullptr)
		return;

	// Simple crosshair so P2 is visually distinct from the touch/mouse cursor.
	int x = GetX(), y = GetY();
	g->SetColor(Color(0, 160, 255, 230));
	g->FillRect(x - 12, y - 2, 24, 4);
	g->FillRect(x - 2, y - 12, 4, 24);
}
