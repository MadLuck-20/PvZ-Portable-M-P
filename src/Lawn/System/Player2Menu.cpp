#include "Player2Menu.h"
#include "Player2Input.h"
#include "../../LawnApp.h"
#include "../../Resources.h"
#include "../Board.h"
#include "widget/WidgetManager.h"
#include "widget/Dialog.h"
#include "widget/Slider.h"
#include "widget/SliderListener.h"
#include "graphics/Graphics.h"
#include <algorithm>
#include <cmath>

using namespace Sexy;

namespace
{
	enum class Owner { None, P1, P2 };

	Owner		sOwner = Owner::None;
	bool		sP2Opening = false;		// player 2 is the one calling DoNewOptions right now
	bool		sDialogsOpen = false;
	bool		sNeedCenter = false;

	float		sCursorX = 400.0f;
	float		sCursorY = 300.0f;
	int			sSavedMouseX = 0;
	int			sSavedMouseY = 0;

	Slider*		sSlider = nullptr;		// slider being adjusted (confirm with X, cancel with O)
	double		sSliderOriginal = 0.0;

	int			sReleaseCountdown = 0;	// a click is held down for a few ticks so it looks pressed
	int			sReleaseX = 0;
	int			sReleaseY = 0;

	int			sDPadHold[4] = { 0, 0, 0, 0 };

	constexpr float kCursorSpeed = 7.0f;
	constexpr int	kDPadStep = 28;
	constexpr double kSliderStickSpeed = 0.012;
	constexpr double kSliderDPadStep = 0.02;

	const SDL_GameControllerButton kDirButtons[4] = {
		SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
		SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT };

	void TakeOwnership(LawnApp* theApp)
	{
		sOwner = Owner::P2;
		sSlider = nullptr;
		sReleaseCountdown = 0;
		sNeedCenter = true;
		if (theApp != nullptr && theApp->mWidgetManager != nullptr)
		{
			sSavedMouseX = theApp->mWidgetManager->mLastMouseX;
			sSavedMouseY = theApp->mWidgetManager->mLastMouseY;
		}
		for (int& aHold : sDPadHold)
			aHold = 0;
	}

	void ReleaseOwnership(LawnApp* theApp, bool theRestoreMouse)
	{
		if (sOwner == Owner::P2 && sReleaseCountdown > 0 && theApp != nullptr && theApp->mWidgetManager != nullptr)
			theApp->mWidgetManager->MouseUp(sReleaseX, sReleaseY, 1);

		if (sOwner == Owner::P2 && theRestoreMouse && theApp != nullptr && theApp->mWidgetManager != nullptr)
			theApp->mWidgetManager->MouseMove(sSavedMouseX, sSavedMouseY);

		sOwner = Owner::None;
		sSlider = nullptr;
		sReleaseCountdown = 0;
		sNeedCenter = false;
	}

	// Options button: opens the same pause menu the on-screen menu button opens.
	void TryOpenPause(LawnApp* theApp)
	{
		if (!Player2Input::IsConnected() || !Player2Input::JustPressed(SDL_CONTROLLER_BUTTON_START))
			return;

		Board* aBoard = theApp->mBoard;
		if (aBoard == nullptr || theApp->mGameScene != GameScenes::SCENE_PLAYING)
			return;
		if (theApp->mGameMode == GameMode::GAMEMODE_CHALLENGE_ZEN_GARDEN ||
			theApp->mGameMode == GameMode::GAMEMODE_TREE_OF_WISDOM ||
			theApp->mGameMode == GameMode::GAMEMODE_UPSELL)
			return;
		if (!aBoard->CanInteractWithBoardButtons() ||
			theApp->GetDialog(Dialogs::DIALOG_GAME_OVER) || theApp->GetDialog(Dialogs::DIALOG_LEVEL_COMPLETE))
			return;

		sP2Opening = true;
		theApp->PlaySample(Sexy::SOUND_PAUSE);
		theApp->DoNewOptions(false);
		sP2Opening = false;
	}

	void SetSliderValue(Slider* theSlider, double theValue)
	{
		theSlider->SetValue(theValue);
		if (theSlider->mListener != nullptr)
			theSlider->mListener->SliderVal(theSlider->mId, theSlider->mVal);
	}

	// Slider "adjust mode": nothing else in the menu reacts until X (keep) or O (undo).
	void RunSliderAdjust()
	{
		if (Player2Input::JustPressed(SDL_CONTROLLER_BUTTON_A))
		{
			sSlider = nullptr;
			return;
		}
		if (Player2Input::JustPressed(SDL_CONTROLLER_BUTTON_B))
		{
			SetSliderValue(sSlider, sSliderOriginal);
			sSlider = nullptr;
			return;
		}

		float aStickX = 0.0f, aStickY = 0.0f;
		Player2Input::GetLeftStick(aStickX, aStickY);
		double aDelta = (sSlider->mHorizontal ? aStickX : -aStickY) * kSliderStickSpeed;

		// D-pad: one step per press, repeating while held. Index 2/3 = left/right, 0/1 = up/down.
		const int aMinus = sSlider->mHorizontal ? 2 : 1;
		const int aPlus = sSlider->mHorizontal ? 3 : 0;
		for (int i = 0; i < 4; i++)
		{
			if (Player2Input::IsDown(kDirButtons[i]))
			{
				if (sDPadHold[i] == 0 || (sDPadHold[i] > 20 && (sDPadHold[i] - 20) % 3 == 0))
				{
					if (i == aMinus)
						aDelta -= kSliderDPadStep;
					else if (i == aPlus)
						aDelta += kSliderDPadStep;
				}
				sDPadHold[i]++;
			}
			else
				sDPadHold[i] = 0;
		}

		if (aDelta != 0.0)
			SetSliderValue(sSlider, std::clamp(sSlider->mVal + aDelta, 0.0, 1.0));
	}

	void RunP2(LawnApp* theApp)
	{
		WidgetManager* aWM = theApp->mWidgetManager;

		if (sNeedCenter)
		{
			Dialog* aDialog = theApp->GetDialog(Dialogs::DIALOG_NEWOPTIONS);
			if (aDialog != nullptr)
			{
				sCursorX = static_cast<float>(aDialog->mX + aDialog->mWidth / 2);
				sCursorY = static_cast<float>(aDialog->mY + aDialog->mHeight / 2);
			}
			else
			{
				sCursorX = theApp->mWidth / 2.0f;
				sCursorY = theApp->mHeight / 2.0f;
			}
			aWM->MouseMove(static_cast<int>(sCursorX), static_cast<int>(sCursorY));
			sNeedCenter = false;
			return; // the press that opened the menu must not also act inside it
		}

		// Redraw everything every tick so the overlay cursor never leaves a trail.
		aWM->MarkAllDirty();

		// A click is held for a few ticks, then released.
		if (sReleaseCountdown > 0)
		{
			if (--sReleaseCountdown == 0)
				aWM->MouseUp(sReleaseX, sReleaseY, 1);
			return;
		}

		if (sSlider != nullptr)
		{
			RunSliderAdjust();
			return;
		}

		// Options again closes the pause menu (only when it is the only dialog open).
		if (Player2Input::JustPressed(SDL_CONTROLLER_BUTTON_START))
		{
			Dialog* aDialog = theApp->GetDialog(Dialogs::DIALOG_NEWOPTIONS);
			if (aDialog != nullptr && theApp->GetDialogCount() == 1)
				aDialog->KeyDown(KeyCode::KEYCODE_ESCAPE);
			return;
		}

		// Move the cursor: left stick, or the D-pad in steps (repeating while held).
		float aStickX = 0.0f, aStickY = 0.0f;
		Player2Input::GetLeftStick(aStickX, aStickY);
		float aDX = aStickX * kCursorSpeed;
		float aDY = aStickY * kCursorSpeed;
		static const int kDirDX[4] = { 0, 0, -1, 1 };
		static const int kDirDY[4] = { -1, 1, 0, 0 };
		for (int i = 0; i < 4; i++)
		{
			if (Player2Input::IsDown(kDirButtons[i]))
			{
				if (sDPadHold[i] == 0 || (sDPadHold[i] > 25 && (sDPadHold[i] - 25) % 6 == 0))
				{
					aDX += static_cast<float>(kDirDX[i] * kDPadStep);
					aDY += static_cast<float>(kDirDY[i] * kDPadStep);
				}
				sDPadHold[i]++;
			}
			else
				sDPadHold[i] = 0;
		}

		if (aDX != 0.0f || aDY != 0.0f)
		{
			sCursorX = std::clamp(sCursorX + aDX, 0.0f, static_cast<float>(theApp->mWidth - 1));
			sCursorY = std::clamp(sCursorY + aDY, 0.0f, static_cast<float>(theApp->mHeight - 1));
			aWM->MouseMove(static_cast<int>(sCursorX), static_cast<int>(sCursorY));
		}

		// X: press whatever is under the cursor. Sliders are grabbed first instead of jumping.
		if (Player2Input::JustPressed(SDL_CONTROLLER_BUTTON_A))
		{
			const int aX = static_cast<int>(sCursorX);
			const int aY = static_cast<int>(sCursorY);
			int aLocalX = 0, aLocalY = 0;
			Widget* aWidget = aWM->GetWidgetAt(aX, aY, &aLocalX, &aLocalY);
			if (Slider* aSlider = dynamic_cast<Slider*>(aWidget))
			{
				sSlider = aSlider;
				sSliderOriginal = aSlider->mVal;
			}
			else
			{
				aWM->MouseMove(aX, aY);
				aWM->MouseDown(aX, aY, 1);
				sReleaseX = aX;
				sReleaseY = aY;
				sReleaseCountdown = 8;
			}
		}
	}
}

void Player2Menu::OnOptionsOpened()
{
	if (sP2Opening)
		TakeOwnership(gLawnApp);
	else
		sOwner = Owner::P1;
}

bool Player2Menu::BlocksPointerInput()
{
	return sOwner == Owner::P2;
}

void Player2Menu::NotifyPointerDown()
{
	// An unowned dialog is taken by whoever touches it first.
	if (sDialogsOpen && sOwner == Owner::None)
		sOwner = Owner::P1;
}

bool Player2Menu::IsActive()
{
	return sOwner == Owner::P2 && sDialogsOpen;
}

void Player2Menu::Update(LawnApp* theApp)
{
	if (theApp == nullptr || theApp->mWidgetManager == nullptr)
		return;

	sDialogsOpen = theApp->GetDialogCount() > 0;

	if (!sDialogsOpen)
	{
		if (sOwner != Owner::None)
			ReleaseOwnership(theApp, true);
		TryOpenPause(theApp);
		return;
	}

	if (!Player2Input::IsConnected())
	{
		if (sOwner == Owner::P2)
			ReleaseOwnership(theApp, false); // pad lost: give the menu back to touch
		return;
	}

	if (sOwner == Owner::P1)
		return;

	if (sOwner == Owner::None)
	{
		// Nobody opened this dialog on purpose: X claims it.
		if (Player2Input::JustPressed(SDL_CONTROLLER_BUTTON_A))
			TakeOwnership(theApp);
		return;
	}

	RunP2(theApp);
}

void Player2Menu::Draw(Sexy::Graphics* g)
{
	if (!IsActive() || sNeedCenter)
		return;

	// Highlight the slider being adjusted, with a short hint above it.
	if (sSlider != nullptr)
	{
		const Point aPos = sSlider->GetAbsPos();
		g->SetColor(Color(0, 160, 255, 255));
		for (int t = 0; t < 2; t++)
			g->DrawRect(aPos.mX - 3 - t, aPos.mY - 3 - t, sSlider->mWidth + 6 + 2 * t, sSlider->mHeight + 6 + 2 * t);
		g->SetFont(FONT_PICO129);
		g->DrawString("< >   X: OK   O: CANCEL", aPos.mX, aPos.mY - 10);
	}

	// Arrow cursor: dark outline under a blue fill.
	const int x = static_cast<int>(sCursorX);
	const int y = static_cast<int>(sCursorY);
	g->SetColor(Color(0, 0, 0, 255));
	for (int i = 0; i < 24; i++)
		g->FillRect(x - 1, y + i - 1, std::max(3, i * 7 / 10 + 3), 3);
	g->SetColor(Color(0, 160, 255, 255));
	for (int i = 0; i < 22; i++)
		g->FillRect(x, y + i, std::max(1, i * 7 / 10 + 1), 1);
}
