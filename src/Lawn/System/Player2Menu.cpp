#include "Player2Menu.h"
#include "Player2Input.h"
#include "../../LawnApp.h"
#include "../../Resources.h"
#include "../Board.h"
#include "../SeedPacket.h"
#include "../Widget/NewOptionsDialog.h"
#include "../Widget/GameButton.h"
#include "../Widget/SeedChooserScreen.h"
#include "widget/WidgetManager.h"
#include "widget/Dialog.h"
#include "widget/ButtonWidget.h"
#include "widget/Checkbox.h"
#include "widget/Slider.h"
#include "widget/SliderListener.h"
#include "graphics/Graphics.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

using namespace Sexy;

// Console-style menu navigation: there is no mouse. The D-pad or the left stick jumps the
// selection from option to option (buttons, checkboxes, sliders), X activates the selected
// option, and sliders must first be confirmed with X before their value can be changed.

namespace
{
	enum class Owner { None, P1, P2 };

	Owner		sOwner = Owner::None;
	bool		sP2Opening = false;		// player 2 is the one calling DoNewOptions right now
	bool		sDialogsOpen = false;

	Widget*		sFocus = nullptr;		// the selected option (only ever compared, never used unless valid)
	Dialog*		sFocusDialog = nullptr;	// the dialog that option belongs to
	int			sFocusCX = 0;			// last known centre of the selected option
	int			sFocusCY = 0;
	bool		sFocusDrawable = false;	// false right after an action that may have destroyed widgets

	int			sSavedMouseX = 0;
	int			sSavedMouseY = 0;

	Slider*		sSlider = nullptr;		// slider being adjusted
	double		sSliderOriginal = 0.0;

	int			sReleaseCountdown = 0;	// a press is held for a few ticks so it looks pressed
	int			sReleaseX = 0;
	int			sReleaseY = 0;

	int			sDirHold[4] = { 0, 0, 0, 0 };	// 0 up, 1 down, 2 left, 3 right

	constexpr float	kStickThreshold = 0.6f;
	constexpr int	kRepeatDelay = 28;
	constexpr int	kRepeatEvery = 9;
	constexpr double kSliderStickSpeed = 0.012;
	constexpr double kSliderDPadStep = 0.02;

	const SDL_GameControllerButton kDirButtons[4] = {
		SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN,
		SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT };

	// ---- shared "jump to the neighbouring option" search ----

	struct NavItem
	{
		int mCX;
		int mCY;
	};

	// Directions: 0 up, 1 down, 2 left, 3 right. Moves one row (or column) at a time: it first
	// finds the nearest row in that direction and only then prefers the option lined up with
	// the current one, so nothing in between is ever skipped. Returns -1 if there is none.
	int FindNext(const std::vector<NavItem>& theItems, int theCurrent, int theDirection)
	{
		const bool aVertical = theDirection < 2;
		const int kSidewaysLimit = 40;	// left/right moves stay on (roughly) the same row
		const int kBand = aVertical ? 14 : 20;
		const NavItem& aCur = theItems[theCurrent];

		auto Measure = [&](const NavItem& theItem, int& thePrimary, int& theSideways)
		{
			const int aDX = theItem.mCX - aCur.mCX;
			const int aDY = theItem.mCY - aCur.mCY;
			switch (theDirection)
			{
			case 0:  thePrimary = -aDY; theSideways = std::abs(aDX); break;
			case 1:  thePrimary = aDY;  theSideways = std::abs(aDX); break;
			case 2:  thePrimary = -aDX; theSideways = std::abs(aDY); break;
			default: thePrimary = aDX;  theSideways = std::abs(aDY); break;
			}
		};

		int aMinPrimary = -1;
		for (int i = 0; i < static_cast<int>(theItems.size()); i++)
		{
			if (i == theCurrent)
				continue;
			int aPrimary, aSideways;
			Measure(theItems[i], aPrimary, aSideways);
			if (aPrimary < 3 || (!aVertical && aSideways > kSidewaysLimit))
				continue;
			if (aMinPrimary < 0 || aPrimary < aMinPrimary)
				aMinPrimary = aPrimary;
		}
		if (aMinPrimary < 0)
			return -1;

		int aBest = -1;
		int aBestSideways = 0;
		for (int i = 0; i < static_cast<int>(theItems.size()); i++)
		{
			if (i == theCurrent)
				continue;
			int aPrimary, aSideways;
			Measure(theItems[i], aPrimary, aSideways);
			if (aPrimary < 3 || (!aVertical && aSideways > kSidewaysLimit) || aPrimary > aMinPrimary + kBand)
				continue;
			if (aBest < 0 || aSideways < aBestSideways)
			{
				aBest = i;
				aBestSideways = aSideways;
			}
		}
		return aBest;
	}

	void ResetHold()
	{
		for (int& aHold : sDirHold)
			aHold = 0;
	}

	void TakeOwnership(LawnApp* theApp)
	{
		sOwner = Owner::P2;
		sSlider = nullptr;
		sReleaseCountdown = 0;
		sFocus = nullptr;
		sFocusDialog = nullptr;
		sFocusDrawable = false;
		if (theApp != nullptr && theApp->mWidgetManager != nullptr)
		{
			sSavedMouseX = theApp->mWidgetManager->mLastMouseX;
			sSavedMouseY = theApp->mWidgetManager->mLastMouseY;
		}
		ResetHold();
	}

	void ReleaseOwnership(LawnApp* theApp, bool theRestoreMouse)
	{
		if (sOwner == Owner::P2 && sReleaseCountdown > 0 && theApp != nullptr && theApp->mWidgetManager != nullptr)
			theApp->mWidgetManager->MouseUp(sReleaseX, sReleaseY, 1);

		if (sOwner == Owner::P2 && theRestoreMouse && theApp != nullptr && theApp->mWidgetManager != nullptr)
			theApp->mWidgetManager->MouseMove(sSavedMouseX, sSavedMouseY);

		sOwner = Owner::None;
		sSlider = nullptr;
		sFocus = nullptr;
		sFocusDialog = nullptr;
		sFocusDrawable = false;
		sReleaseCountdown = 0;
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

	// ---- option list and spatial navigation ----

	void CollectItems(Widget* theContainer, std::vector<Widget*>& theItems)
	{
		for (Widget* aWidget : theContainer->mWidgets)
		{
			if (aWidget == nullptr || !aWidget->mVisible || aWidget->mDisabled)
				continue;

			if (dynamic_cast<ButtonWidget*>(aWidget) != nullptr ||
				dynamic_cast<Checkbox*>(aWidget) != nullptr ||
				dynamic_cast<Slider*>(aWidget) != nullptr)
				theItems.push_back(aWidget);

			CollectItems(aWidget, theItems);
		}
	}

	void CenterOf(Widget* theWidget, int& theX, int& theY)
	{
		const Point aPos = theWidget->GetAbsPos();
		theX = aPos.mX + theWidget->mWidth / 2;
		theY = aPos.mY + theWidget->mHeight / 2;
	}

	void SetFocus(LawnApp* theApp, Widget* theWidget, Dialog* theDialog, bool theSyncMouse)
	{
		sFocus = theWidget;
		sFocusDialog = theDialog;
		CenterOf(theWidget, sFocusCX, sFocusCY);
		// The game's own hover highlight follows the selection.
		if (theSyncMouse)
			theApp->mWidgetManager->MouseMove(sFocusCX, sFocusCY);
	}

	Widget* PickInitialFocus(Dialog* theDialog, const std::vector<Widget*>& theItems)
	{
		// Pause menu: start on "Back to game", like the console versions start on Resume.
		if (NewOptionsDialog* anOptions = dynamic_cast<NewOptionsDialog*>(theDialog))
		{
			Widget* aBack = anOptions->mBackToGameButton.get();
			if (std::find(theItems.begin(), theItems.end(), aBack) != theItems.end())
				return aBack;
		}
		return theItems.front();
	}

	Widget* PickNearest(const std::vector<Widget*>& theItems, int theX, int theY)
	{
		Widget* aBest = theItems.front();
		long long aBestDist = -1;
		for (Widget* aWidget : theItems)
		{
			int aCX, aCY;
			CenterOf(aWidget, aCX, aCY);
			const long long aDX = aCX - theX, aDY = aCY - theY;
			const long long aDist = aDX * aDX + aDY * aDY;
			if (aBestDist < 0 || aDist < aBestDist)
			{
				aBestDist = aDist;
				aBest = aWidget;
			}
		}
		return aBest;
	}

	// Move the selection to the neighbouring option in a direction (0 up, 1 down, 2 left, 3 right).
	bool MoveFocus(LawnApp* theApp, const std::vector<Widget*>& theItems, Dialog* theDialog, int theDirection)
	{
		std::vector<NavItem> aNav;
		int aCurrent = -1;
		for (int i = 0; i < static_cast<int>(theItems.size()); i++)
		{
			NavItem aItem;
			CenterOf(theItems[i], aItem.mCX, aItem.mCY);
			aNav.push_back(aItem);
			if (theItems[i] == sFocus)
				aCurrent = i;
		}
		if (aCurrent < 0)
			return false;

		const int aNext = FindNext(aNav, aCurrent, theDirection);
		if (aNext < 0)
			return false;

		SetFocus(theApp, theItems[aNext], theDialog, true);
		theApp->PlaySample(Sexy::SOUND_BUTTONCLICK);
		return true;
	}

	// ---- sliders ----

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
			ResetHold();
			return;
		}
		if (Player2Input::JustPressed(SDL_CONTROLLER_BUTTON_B))
		{
			SetSliderValue(sSlider, sSliderOriginal);
			sSlider = nullptr;
			ResetHold();
			return;
		}

		float aStickX = 0.0f, aStickY = 0.0f;
		Player2Input::GetLeftStick(aStickX, aStickY);
		double aDelta = (sSlider->mHorizontal ? aStickX : -aStickY) * kSliderStickSpeed;

		// D-pad: one step per press, repeating while held.
		const int aMinus = sSlider->mHorizontal ? 2 : 1;
		const int aPlus = sSlider->mHorizontal ? 3 : 0;
		for (int i = 0; i < 4; i++)
		{
			if (Player2Input::IsDown(kDirButtons[i]))
			{
				if (sDirHold[i] == 0 || (sDirHold[i] > 20 && (sDirHold[i] - 20) % 3 == 0))
				{
					if (i == aMinus)
						aDelta -= kSliderDPadStep;
					else if (i == aPlus)
						aDelta += kSliderDPadStep;
				}
				sDirHold[i]++;
			}
			else
				sDirHold[i] = 0;
		}

		if (aDelta != 0.0)
			SetSliderValue(sSlider, std::clamp(sSlider->mVal + aDelta, 0.0, 1.0));
	}

	// ---- seed chooser (the plant selection screen shown before a level) ----
	//
	// Player 2 moves a blue frame over the plant slots with the D-pad / left stick, X picks the
	// plant under the frame, and O takes back the last plant player 2 picked (the frame then goes
	// back to that plant's slot). When the bank is full the frame moves to the Start button: X
	// starts the level, O takes back the last pick. Touch keeps working at the same time.

	bool					sChooserActive = false;
	SeedChooserScreen*		sChooserPtr = nullptr;	// only ever compared, never used unless it is the live screen
	SeedType				sChooserSeed = SEED_PEASHOOTER;
	bool					sChooserOnStart = false;
	std::vector<SeedType>	sChooserPicks;			// player 2's picks, oldest first
	int						sChooserHold[4] = { 0, 0, 0, 0 };

	void ResetChooser()
	{
		sChooserActive = false;
		sChooserPtr = nullptr;
		sChooserSeed = SEED_PEASHOOTER;
		sChooserOnStart = false;
		sChooserPicks.clear();
		for (int& aHold : sChooserHold)
			aHold = 0;
	}

	bool BankIsFull(SeedChooserScreen* theChooser)
	{
		return theChooser->mSeedsInBank == theChooser->mBoard->mSeedBank->mNumPackets;
	}

	// Seeds still flying are landed first, exactly like a click on the screen does.
	void LandFlyingSeeds(SeedChooserScreen* theChooser)
	{
		if (theChooser->mSeedsInFlight > 0)
		{
			for (int i = 0; i < NUM_SEEDS_IN_CHOOSER; i++)
				theChooser->LandFlyingSeed(theChooser->mChosenSeeds[i]);
		}
	}

	void PickSeed(LawnApp* theApp, SeedChooserScreen* theChooser, SeedType theSeed)
	{
		LandFlyingSeeds(theChooser);
		if (!theApp->HasSeedType(theSeed))
			return;
		if (theChooser->SeedNotAllowedToPick(theSeed) || theChooser->SeedNotAllowedDuringTrial(theSeed))
		{
			theApp->PlaySample(Sexy::SOUND_BUZZER);
			return;
		}

		ChosenSeed& aChosen = theChooser->mChosenSeeds[theSeed];
		if (aChosen.mSeedState != SEED_IN_CHOOSER || BankIsFull(theChooser))
			return;

		theChooser->ClickedSeedInChooser(aChosen);
		if (aChosen.mSeedState == SEED_FLYING_TO_BANK)
		{
			sChooserPicks.push_back(theSeed);
			if (BankIsFull(theChooser))
				sChooserOnStart = true; // the bank is full: the frame moves to the Start button
		}
	}

	void UndoPick(LawnApp* theApp, SeedChooserScreen* theChooser)
	{
		LandFlyingSeeds(theChooser);
		while (!sChooserPicks.empty())
		{
			const SeedType aSeed = sChooserPicks.back();
			ChosenSeed& aChosen = theChooser->mChosenSeeds[aSeed];
			if (aChosen.mSeedState == SEED_IN_BANK)
			{
				if (aChosen.mCrazyDavePicked)
				{
					theApp->PlaySample(Sexy::SOUND_BUZZER);
					return;
				}
				theChooser->ClickedSeedInBank(aChosen);
				sChooserPicks.pop_back();
				sChooserSeed = aSeed;		// the frame goes back to the plant that was taken back
				sChooserOnStart = false;
				return;
			}
			sChooserPicks.pop_back(); // no longer in the bank (taken back by touch)
		}
	}

	void RunChooser(LawnApp* theApp, SeedChooserScreen* theChooser)
	{
		if (!Player2Input::IsConnected() || !theChooser->mVisible || !theChooser->mMouseVisible ||
			theChooser->mChooseState != CHOOSE_NORMAL || theChooser->mBoard == nullptr)
		{
			sChooserActive = false;
			return;
		}

		if (sChooserPtr != theChooser)
		{
			ResetChooser();
			sChooserPtr = theChooser;
		}
		sChooserActive = true;
		theApp->mWidgetManager->MarkAllDirty(); // redraw every tick so the frame leaves no trail

		const bool aStartEnabled = !theChooser->mStartButton->mDisabled;
		if (!aStartEnabled || !BankIsFull(theChooser))
			sChooserOnStart = false;
		if (!theApp->HasSeedType(sChooserSeed))
			sChooserSeed = SEED_PEASHOOTER;

		// Every owned plant slot, plus the Start button while it can be pressed.
		std::vector<NavItem> aNav;
		std::vector<int> aIds;		// plant type, or -1 for the Start button
		int aCurrent = -1;
		for (int aType = 0; aType < NUM_SEEDS_IN_CHOOSER; aType++)
		{
			if (!theApp->HasSeedType(static_cast<SeedType>(aType)))
				continue;
			int aX, aY;
			theChooser->GetSeedPositionInChooser(aType, aX, aY);
			aNav.push_back({ aX + SEED_PACKET_WIDTH / 2, aY + SEED_PACKET_HEIGHT / 2 });
			aIds.push_back(aType);
			if (!sChooserOnStart && aType == static_cast<int>(sChooserSeed))
				aCurrent = static_cast<int>(aIds.size()) - 1;
		}
		if (aStartEnabled)
		{
			aNav.push_back({ theChooser->mStartButton->mX + theChooser->mStartButton->mWidth / 2,
				theChooser->mStartButton->mY + theChooser->mStartButton->mHeight / 2 });
			aIds.push_back(-1);
			if (sChooserOnStart)
				aCurrent = static_cast<int>(aIds.size()) - 1;
		}
		if (aCurrent < 0)
		{
			if (aNav.empty())
				return;
			aCurrent = 0;
		}

		// O: take back the last plant player 2 picked.
		if (Player2Input::JustPressed(SDL_CONTROLLER_BUTTON_B))
		{
			UndoPick(theApp, theChooser);
			return;
		}

		// X: pick the plant under the frame, or start the level when the frame is on Start.
		if (Player2Input::JustPressed(SDL_CONTROLLER_BUTTON_A))
		{
			if (sChooserOnStart)
			{
				LandFlyingSeeds(theChooser);
				theApp->PlaySample(Sexy::SOUND_TAP);
				sChooserActive = false;
				// 100 is SeedChooserScreen_Start (a private enumerator). This may open a
				// confirmation dialog or close the screen, so nothing may touch it afterwards.
				theChooser->ButtonDepress(100);
				return;
			}
			PickSeed(theApp, theChooser, sChooserSeed);
			return;
		}

		// D-pad or left stick: jump to the neighbouring slot, repeating while held.
		float aStickX = 0.0f, aStickY = 0.0f;
		Player2Input::GetLeftStick(aStickX, aStickY);
		const bool aVertical = std::fabs(aStickY) >= std::fabs(aStickX);
		const bool aActive[4] = {
			Player2Input::IsDown(kDirButtons[0]) || (aVertical && aStickY < -kStickThreshold),
			Player2Input::IsDown(kDirButtons[1]) || (aVertical && aStickY > kStickThreshold),
			Player2Input::IsDown(kDirButtons[2]) || (!aVertical && aStickX < -kStickThreshold),
			Player2Input::IsDown(kDirButtons[3]) || (!aVertical && aStickX > kStickThreshold) };
		for (int i = 0; i < 4; i++)
		{
			if (aActive[i])
			{
				if (sChooserHold[i] == 0 || (sChooserHold[i] > kRepeatDelay && (sChooserHold[i] - kRepeatDelay) % kRepeatEvery == 0))
				{
					const int aNext = FindNext(aNav, aCurrent, i);
					if (aNext >= 0)
					{
						if (aIds[aNext] < 0)
							sChooserOnStart = true;
						else
						{
							sChooserOnStart = false;
							sChooserSeed = static_cast<SeedType>(aIds[aNext]);
						}
						aCurrent = aNext;
						theApp->PlaySample(Sexy::SOUND_BUTTONCLICK);
					}
				}
				sChooserHold[i]++;
			}
			else
				sChooserHold[i] = 0;
		}
	}

	void CloseOptions(Dialog* theDialog)
	{
		theDialog->KeyDown(KeyCode::KEYCODE_ESCAPE); // same as Esc: back to the game
		sFocus = nullptr;
		sFocusDrawable = false;
		sSlider = nullptr;
	}

	void RunP2(LawnApp* theApp)
	{
		WidgetManager* aWM = theApp->mWidgetManager.get();
		if (theApp->mDialogList.empty())
			return;
		Dialog* aTop = theApp->mDialogList.back();

		// Redraw everything every tick so the selection frame never leaves a trail.
		aWM->MarkAllDirty();

		// A press is held for a few ticks, then released (which may close the dialog).
		if (sReleaseCountdown > 0)
		{
			if (--sReleaseCountdown == 0)
			{
				aWM->MouseUp(sReleaseX, sReleaseY, 1);
				sFocusDrawable = false; // widgets may be gone; re-validated next tick
			}
			return;
		}

		std::vector<Widget*> aItems;
		CollectItems(aTop, aItems);
		if (aItems.empty())
		{
			sFocus = nullptr;
			sFocusDrawable = false;
			return;
		}

		// Keep the selection valid. If its dialog was replaced, start fresh on the new one;
		// if the option itself vanished, take the closest remaining one.
		if (std::find(aItems.begin(), aItems.end(), sFocus) == aItems.end() || sFocusDialog != aTop)
		{
			Widget* aPick = (sFocusDialog == aTop && sFocus != nullptr)
				? PickNearest(aItems, sFocusCX, sFocusCY)
				: PickInitialFocus(aTop, aItems);
			SetFocus(theApp, aPick, aTop, true);
		}
		else
		{
			CenterOf(sFocus, sFocusCX, sFocusCY);
		}
		sFocusDrawable = true;

		if (sSlider != nullptr && std::find(aItems.begin(), aItems.end(), sSlider) == aItems.end())
			sSlider = nullptr;

		if (sSlider != nullptr)
		{
			RunSliderAdjust();
			return;
		}

		// Options (or O) closes the pause menu when it is the only dialog open.
		if (theApp->GetDialogCount() == 1 && dynamic_cast<NewOptionsDialog*>(aTop) != nullptr &&
			(Player2Input::JustPressed(SDL_CONTROLLER_BUTTON_START) || Player2Input::JustPressed(SDL_CONTROLLER_BUTTON_B)))
		{
			CloseOptions(aTop);
			return;
		}

		// D-pad or left stick: jump to the neighbouring option, repeating while held.
		float aStickX = 0.0f, aStickY = 0.0f;
		Player2Input::GetLeftStick(aStickX, aStickY);
		const bool aVertical = std::fabs(aStickY) >= std::fabs(aStickX);
		const bool aActive[4] = {
			Player2Input::IsDown(kDirButtons[0]) || (aVertical && aStickY < -kStickThreshold),
			Player2Input::IsDown(kDirButtons[1]) || (aVertical && aStickY > kStickThreshold),
			Player2Input::IsDown(kDirButtons[2]) || (!aVertical && aStickX < -kStickThreshold),
			Player2Input::IsDown(kDirButtons[3]) || (!aVertical && aStickX > kStickThreshold) };
		for (int i = 0; i < 4; i++)
		{
			if (aActive[i])
			{
				if (sDirHold[i] == 0 || (sDirHold[i] > kRepeatDelay && (sDirHold[i] - kRepeatDelay) % kRepeatEvery == 0))
					MoveFocus(theApp, aItems, aTop, i);
				sDirHold[i]++;
			}
			else
				sDirHold[i] = 0;
		}

		// X: activate the selected option. Sliders are grabbed first instead of jumping.
		if (Player2Input::JustPressed(SDL_CONTROLLER_BUTTON_A) && sFocus != nullptr)
		{
			if (Slider* aSlider = dynamic_cast<Slider*>(sFocus))
			{
				sSlider = aSlider;
				sSliderOriginal = aSlider->mVal;
				ResetHold();
			}
			else
			{
				CenterOf(sFocus, sReleaseX, sReleaseY);
				aWM->MouseMove(sReleaseX, sReleaseY);
				aWM->MouseDown(sReleaseX, sReleaseY, 1);
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
	return (sOwner == Owner::P2 && sDialogsOpen) || sChooserActive;
}

void Player2Menu::Update(LawnApp* theApp)
{
	if (theApp == nullptr || theApp->mWidgetManager == nullptr)
		return;

	sDialogsOpen = theApp->GetDialogCount() > 0;

	// The plant selection screen shown before a level. Dialogs opened over it (almanac,
	// confirmations...) are handled by the menu code below.
	SeedChooserScreen* aChooser = theApp->mSeedChooserScreen.get();
	if (aChooser == nullptr)
		ResetChooser();
	else if (sDialogsOpen)
		sChooserActive = false;
	else
		RunChooser(theApp, aChooser);

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
	// Seed chooser: blue frame on the slot (or the Start button) player 2 is on, with its ID.
	// The pointer is checked against the live screen first, since touch can close it at any time.
	if (sChooserActive && sChooserPtr != nullptr && gLawnApp != nullptr && sChooserPtr == gLawnApp->mSeedChooserScreen.get())
	{
		SeedChooserScreen* aChooser = sChooserPtr;
		const Point aBase = aChooser->GetAbsPos();
		int aX, aY, aW, aH;
		if (sChooserOnStart)
		{
			aX = aBase.mX + aChooser->mStartButton->mX;
			aY = aBase.mY + aChooser->mStartButton->mY;
			aW = aChooser->mStartButton->mWidth;
			aH = aChooser->mStartButton->mHeight;
		}
		else
		{
			int aSlotX, aSlotY;
			aChooser->GetSeedPositionInChooser(sChooserSeed, aSlotX, aSlotY);
			aX = aBase.mX + aSlotX;
			aY = aBase.mY + aSlotY;
			aW = SEED_PACKET_WIDTH;
			aH = SEED_PACKET_HEIGHT;
		}

		g->SetColor(Color(0, 160, 255, 255));
		for (int t = 0; t < 3; t++)
			g->DrawRect(aX - 3 - t, aY - 3 - t, aW + 6 + 2 * t, aH + 6 + 2 * t);
		g->SetFont(FONT_PICO129);
		g->DrawString("P2", aX + 4, (aY >= 16) ? aY - 6 : aY + aH + 14);
	}

	// The game's own button highlight already shows the selected menu option, so only the
	// hint for an adjusting slider is drawn.
	if (!(sOwner == Owner::P2 && sDialogsOpen) || !sFocusDrawable || sFocus == nullptr || sSlider == nullptr)
		return;

	const Point aPos = sFocus->GetAbsPos();
	g->SetColor(Color(0, 160, 255, 255));
	g->SetFont(FONT_PICO129);
	g->DrawString("< >   X: OK   O: CANCEL", aPos.mX, aPos.mY - 12);
}
