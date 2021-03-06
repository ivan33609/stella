//============================================================================
//
//   SSSS    tt          lll  lll
//  SS  SS   tt           ll   ll
//  SS     tttttt  eeee   ll   ll   aaaa
//   SSSS    tt   ee  ee  ll   ll      aa
//      SS   tt   eeeeee  ll   ll   aaaaa  --  "An Atari 2600 VCS Emulator"
//  SS  SS   tt   ee      ll   ll  aa  aa
//   SSSS     ttt  eeeee llll llll  aaaaa
//
// Copyright (c) 1995-2017 by Bradford W. Mott, Stephen Anthony
// and the Stella Team
//
// See the file "License.txt" for information on usage and redistribution of
// this file, and for a DISCLAIMER OF ALL WARRANTIES.
//============================================================================

#ifndef GAME_INFO_DIALOG_HXX
#define GAME_INFO_DIALOG_HXX

class OSystem;
class GuiObject;
class EditTextWidget;
class PopUpWidget;
class StaticTextWidget;
class TabWidget;
class SliderWidget;

#include "Dialog.hxx"
#include "Command.hxx"
#include "Props.hxx"

class GameInfoDialog : public Dialog, public CommandSender
{
  public:
    GameInfoDialog(OSystem& osystem, DialogContainer& parent,
                   const GUI::Font& font, GuiObject* boss);
    virtual ~GameInfoDialog() = default;

  private:
    void loadConfig() override;
    void saveConfig() override;
    void handleCommand(CommandSender* sender, int cmd, int data, int id) override;

    void setDefaults() override;
    void loadView();

  private:
    TabWidget* myTab;

    // Cartridge properties
    EditTextWidget*   myName;
    StaticTextWidget* myMD5;
    EditTextWidget*   myManufacturer;
    EditTextWidget*   myModelNo;
    EditTextWidget*   myRarity;
    EditTextWidget*   myNote;
    PopUpWidget*      mySound;
    PopUpWidget*      myType;

    // Console properties
    PopUpWidget* myLeftDiff;
    PopUpWidget* myRightDiff;
    PopUpWidget* myTVType;

    // Controller properties
    PopUpWidget*      myP0Controller;
    PopUpWidget*      myP1Controller;
    PopUpWidget*      mySwapPaddles;
    PopUpWidget*      myLeftPort;
    PopUpWidget*      myRightPort;
    PopUpWidget*      myMouseControl;
    PopUpWidget*      myMouseX;
    PopUpWidget*      myMouseY;
    SliderWidget*     myMouseRange;
    StaticTextWidget* myMouseRangeLabel;

    // Display properties
    PopUpWidget*      myFormat;
    SliderWidget*     myYStart;
    StaticTextWidget* myYStartLabel;
    SliderWidget*     myHeight;
    StaticTextWidget* myHeightLabel;
    PopUpWidget*      myPhosphor;
    SliderWidget*     myPPBlend;
    StaticTextWidget* myPPBlendLabel;

    enum {
      kLeftCChanged    = 'LCch',
      kRightCChanged   = 'RCch',
      kMRangeChanged   = 'MRch',
      kYStartChanged   = 'YSch',
      kHeightChanged   = 'HTch',
      kPhosphorChanged = 'PPch',
      kPPBlendChanged  = 'PBch',
      kMCtrlChanged    = 'MCch'
    };

    // Game properties for currently loaded ROM
    Properties myGameProperties;

    // Indicates that we've got a valid properties entry
    bool myPropertiesLoaded;

    // Indicates that the default properties have been loaded
    bool myDefaultsSelected;

  private:
    // Following constructors and assignment operators not supported
    GameInfoDialog() = delete;
    GameInfoDialog(const GameInfoDialog&) = delete;
    GameInfoDialog(GameInfoDialog&&) = delete;
    GameInfoDialog& operator=(const GameInfoDialog&) = delete;
    GameInfoDialog& operator=(GameInfoDialog&&) = delete;
};

#endif
