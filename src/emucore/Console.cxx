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

#include <cassert>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <fstream>

#include "AtariVox.hxx"
#include "Booster.hxx"
#include "Cart.hxx"
#include "Control.hxx"
#include "Cart.hxx"
#include "Driving.hxx"
#include "Event.hxx"
#include "EventHandler.hxx"
#include "Joystick.hxx"
#include "Keyboard.hxx"
#include "KidVid.hxx"
#include "Genesis.hxx"
#include "MindLink.hxx"
#include "CompuMate.hxx"
#include "M6502.hxx"
#include "M6532.hxx"
#include "TIA.hxx"
#include "Paddles.hxx"
#include "Props.hxx"
#include "PropsSet.hxx"
#include "SaveKey.hxx"
#include "Settings.hxx"
#include "Sound.hxx"
#include "Switches.hxx"
#include "System.hxx"
#include "AmigaMouse.hxx"
#include "AtariMouse.hxx"
#include "TrakBall.hxx"
#include "FrameBuffer.hxx"
#include "OSystem.hxx"
#include "Menu.hxx"
#include "CommandMenu.hxx"
#include "Serializable.hxx"
#include "Version.hxx"
#include "FrameManager.hxx"
#include "FrameLayout.hxx"

#ifdef DEBUGGER_SUPPORT
  #include "Debugger.hxx"
#endif

#ifdef CHEATCODE_SUPPORT
  #include "CheatManager.hxx"
#endif

#include "Console.hxx"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Console::Console(OSystem& osystem, unique_ptr<Cartridge>& cart,
                 const Properties& props)
  : myOSystem(osystem),
    myEvent(osystem.eventHandler().event()),
    myProperties(props),
    myCart(std::move(cart)),
    myDisplayFormat(""),  // Unknown TV format @ start
    myFramerate(0.0),     // Unknown framerate @ start
    myCurrentFormat(0),   // Unknown format @ start
    myUserPaletteDefined(false),
    myConsoleTiming(ConsoleTiming::ntsc)
{
  // Load user-defined palette for this ROM
  loadUserPalette();

  // Create subsystems for the console
  my6502 = make_unique<M6502>(myOSystem.settings());
  myRiot = make_unique<M6532>(*this, myOSystem.settings());
  myTIA  = make_unique<TIA>(*this, myOSystem.sound(), myOSystem.settings());
  mySwitches = make_unique<Switches>(myEvent, myProperties);

  // Construct the system and components
  mySystem = make_unique<System>(osystem, *my6502, *myRiot, *myTIA, *myCart);

  // The real controllers for this console will be added later
  // For now, we just add dummy joystick controllers, since autodetection
  // runs the emulation for a while, and this may interfere with 'smart'
  // controllers such as the AVox and SaveKey
  myLeftControl  = make_unique<Joystick>(Controller::Left, myEvent, *mySystem);
  myRightControl = make_unique<Joystick>(Controller::Right, myEvent, *mySystem);

  // We can only initialize after all the devices/components have been created
  mySystem->initialize();

  // Auto-detect NTSC/PAL mode if it's requested
  string autodetected = "";
  myDisplayFormat = myProperties.get(Display_Format);

  // Add the real controllers for this system
  // This must be done before the debugger is initialized
  const string& md5 = myProperties.get(Cartridge_MD5);
  setControllers(md5);

  if(myDisplayFormat == "AUTO" || myOSystem.settings().getBool("rominfo"))
  {
    // Run the TIA, looking for PAL scanline patterns
    // We turn off the SuperCharger progress bars, otherwise the SC BIOS
    // will take over 250 frames!
    // The 'fastscbios' option must be changed before the system is reset
    bool fastscbios = myOSystem.settings().getBool("fastscbios");
    myOSystem.settings().setValue("fastscbios", true);

    uInt8 initialGarbageFrames = FrameManager::initialGarbageFrames();
    uInt8 linesPAL = 0;
    uInt8 linesNTSC = 0;

    mySystem->reset(true);  // autodetect in reset enabled
    myTIA->autodetectLayout(true);
    for(int i = 0; i < 60; ++i) {
      if (i > initialGarbageFrames)
        myTIA->frameLayout() == FrameLayout::pal ? linesPAL++ : linesNTSC++;

      myTIA->update();
    }

    myDisplayFormat = linesPAL > linesNTSC  ? "PAL" : "NTSC";
    if(myProperties.get(Display_Format) == "AUTO")
    {
      autodetected = "*";
      myCurrentFormat = 0;
    }

    // Don't forget to reset the SC progress bars again
    myOSystem.settings().setValue("fastscbios", fastscbios);
  }
  myConsoleInfo.DisplayFormat = myDisplayFormat + autodetected;

  // Set up the correct properties used when toggling format
  // Note that this can be overridden if a format is forced
  //   For example, if a PAL ROM is forced to be NTSC, it will use NTSC-like
  //   properties (60Hz, 262 scanlines, etc), but likely result in flicker
  // The TIA will self-adjust the framerate if necessary
  setTIAProperties();
  if(myDisplayFormat == "NTSC")
  {
    myCurrentFormat = 1;
    myConsoleTiming = ConsoleTiming::ntsc;
  }
  else if(myDisplayFormat == "PAL")
  {
    myCurrentFormat = 2;
    myConsoleTiming = ConsoleTiming::pal;
  }
  else if(myDisplayFormat == "SECAM")
  {
    myCurrentFormat = 3;
    myConsoleTiming = ConsoleTiming::secam;
  }
  else if(myDisplayFormat == "NTSC50")
  {
    myCurrentFormat = 4;
    myConsoleTiming = ConsoleTiming::ntsc;
  }
  else if(myDisplayFormat == "PAL60")
  {
    myCurrentFormat = 5;
    myConsoleTiming = ConsoleTiming::pal;
  }
  else if(myDisplayFormat == "SECAM60")
  {
    myCurrentFormat = 6;
    myConsoleTiming = ConsoleTiming::secam;
  }

  // Bumper Bash always require all 4 directions
  // Other ROMs can use it if the setting is enabled
  // Hopefully this list should stay short
  // If it starts to get too long, we should add a ROM properties entry
  bool joyallow4 = md5 == "aa1c41f86ec44c0a44eb64c332ce08af" || // Bumper Bash
                   md5 == "16ee443c990215f61f7dd1e55a0d2256" || // Bumper Bash (PAL)
                   md5 == "1bf503c724001b09be79c515ecfcbd03" || // Bumper Bash (Unknown)
                   myOSystem.settings().getBool("joyallow4");
  myOSystem.eventHandler().allowAllDirections(joyallow4);

  // Reset the system to its power-on state
  mySystem->reset();

  // Finally, add remaining info about the console
  myConsoleInfo.CartName   = myProperties.get(Cartridge_Name);
  myConsoleInfo.CartMD5    = myProperties.get(Cartridge_MD5);
  myConsoleInfo.Control0   = myLeftControl->about();
  myConsoleInfo.Control1   = myRightControl->about();
  myConsoleInfo.BankSwitch = myCart->about();

  myCart->setRomName(myConsoleInfo.CartName);

  // Let the other devices know about the new console
  mySystem->consoleChanged(myConsoleTiming);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Console::~Console()
{
  // Some smart controllers need to be informed that the console is going away
  myLeftControl->close();
  myRightControl->close();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool Console::save(Serializer& out) const
{
  try
  {
    // First save state for the system
    if(!mySystem->save(out))
      return false;

    // Now save the console controllers and switches
    if(!(myLeftControl->save(out) && myRightControl->save(out) &&
         mySwitches->save(out)))
      return false;
  }
  catch(...)
  {
    cerr << "ERROR: Console::save" << endl;
    return false;
  }

  return true;  // success
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool Console::load(Serializer& in)
{
  try
  {
    // First load state for the system
    if(!mySystem->load(in))
      return false;

    // Then load the console controllers and switches
    if(!(myLeftControl->load(in) && myRightControl->load(in) &&
         mySwitches->load(in)))
      return false;
  }
  catch(...)
  {
    cerr << "ERROR: Console::load" << endl;
    return false;
  }

  return true;  // success
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::toggleFormat(int direction)
{
  string saveformat, message;

  if(direction == 1)
    myCurrentFormat = (myCurrentFormat + 1) % 7;
  else if(direction == -1)
    myCurrentFormat = myCurrentFormat > 0 ? (myCurrentFormat - 1) : 6;

  switch(myCurrentFormat)
  {
    case 0:  // auto-detect
      myTIA->update();
      myDisplayFormat = myTIA->frameLayout() == FrameLayout::pal ? "PAL" : "NTSC";
      message = "Auto-detect mode: " + myDisplayFormat;
      saveformat = "AUTO";
      myConsoleTiming = myTIA->frameLayout() == FrameLayout::pal ?
          ConsoleTiming::pal : ConsoleTiming::ntsc;
      break;
    case 1:
      saveformat = myDisplayFormat = "NTSC";
      myConsoleTiming = ConsoleTiming::ntsc;
      message = "NTSC mode";
      break;
    case 2:
      saveformat = myDisplayFormat = "PAL";
      myConsoleTiming = ConsoleTiming::pal;
      message = "PAL mode";
      break;
    case 3:
      saveformat = myDisplayFormat = "SECAM";
      myConsoleTiming = ConsoleTiming::secam;
      message = "SECAM mode";
      break;
    case 4:
      saveformat = myDisplayFormat = "NTSC50";
      myConsoleTiming = ConsoleTiming::ntsc;
      message = "NTSC50 mode";
      break;
    case 5:
      saveformat = myDisplayFormat = "PAL60";
      myConsoleTiming = ConsoleTiming::pal;
      message = "PAL60 mode";
      break;
    case 6:
      saveformat = myDisplayFormat = "SECAM60";
      myConsoleTiming = ConsoleTiming::secam;
      message = "SECAM60 mode";
      break;
  }
  myProperties.set(Display_Format, saveformat);

  setPalette(myOSystem.settings().getString("palette"));
  setTIAProperties();
  myTIA->frameReset();
  initializeVideo();  // takes care of refreshing the screen

  myOSystem.frameBuffer().showMessage(message);

  // Let the other devices know about the console change
  mySystem->consoleChanged(myConsoleTiming);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::toggleColorLoss()
{
  bool colorloss = !myOSystem.settings().getBool("colorloss");
  if(myTIA->enableColorLoss(colorloss))
  {
    myOSystem.settings().setValue("colorloss", colorloss);
    string message = string("PAL color-loss ") +
                     (colorloss ? "enabled" : "disabled");
    myOSystem.frameBuffer().showMessage(message);
  }
  else
    myOSystem.frameBuffer().showMessage(
      "PAL color-loss not available in non PAL modes");
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::toggleColorLoss(bool state)
{
  myTIA->enableColorLoss(state);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::togglePalette()
{
  string palette, message;
  palette = myOSystem.settings().getString("palette");

  if(palette == "standard")       // switch to z26
  {
    palette = "z26";
    message = "Z26 palette";
  }
  else if(palette == "z26")       // switch to user or standard
  {
    // If we have a user-defined palette, it will come next in
    // the sequence; otherwise loop back to the standard one
    if(myUserPaletteDefined)
    {
      palette = "user";
      message = "User-defined palette";
    }
    else
    {
      palette = "standard";
      message = "Standard Stella palette";
    }
  }
  else if(palette == "user")  // switch to standard
  {
    palette = "standard";
    message = "Standard Stella palette";
  }
  else  // switch to standard mode if we get this far
  {
    palette = "standard";
    message = "Standard Stella palette";
  }

  myOSystem.settings().setValue("palette", palette);
  myOSystem.frameBuffer().showMessage(message);

  setPalette(palette);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::setPalette(const string& type)
{
  // Look at all the palettes, since we don't know which one is
  // currently active
  static uInt32* palettes[3][3] = {
    { &ourNTSCPalette[0],     &ourPALPalette[0],     &ourSECAMPalette[0]     },
    { &ourNTSCPaletteZ26[0],  &ourPALPaletteZ26[0],  &ourSECAMPaletteZ26[0]  },
    { &ourUserNTSCPalette[0], &ourUserPALPalette[0], &ourUserSECAMPalette[0] }
  };

  // See which format we should be using
  int paletteNum = 0;
  if(type == "standard")
    paletteNum = 0;
  else if(type == "z26")
    paletteNum = 1;
  else if(type == "user" && myUserPaletteDefined)
    paletteNum = 2;

  // Now consider the current display format
  const uInt32* palette =
    (myDisplayFormat.compare(0, 3, "PAL") == 0)   ? palettes[paletteNum][1] :
    (myDisplayFormat.compare(0, 5, "SECAM") == 0) ? palettes[paletteNum][2] :
     palettes[paletteNum][0];

  myOSystem.frameBuffer().setPalette(palette);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::togglePhosphor()
{
  if(myOSystem.frameBuffer().tiaSurface().phosphorEnabled())
  {
    myProperties.set(Display_Phosphor, "No");
    myOSystem.frameBuffer().tiaSurface().enablePhosphor(false);
    myOSystem.frameBuffer().showMessage("Phosphor effect disabled");
  }
  else
  {
    myProperties.set(Display_Phosphor, "Yes");
    myOSystem.frameBuffer().tiaSurface().enablePhosphor(true);
    myOSystem.frameBuffer().showMessage("Phosphor effect enabled");
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::changePhosphor(int direction)
{
  int blend = atoi(myProperties.get(Display_PPBlend).c_str());

  if(myOSystem.frameBuffer().tiaSurface().phosphorEnabled())
  {
    if(direction == +1)       // increase blend
    {
      if(blend >= 100)
      {
        myOSystem.frameBuffer().showMessage("Phosphor blend at maximum");
        return;
      }
      else
        blend = std::min(blend+2, 100);
    }
    else if(direction == -1)  // decrease blend
    {
      if(blend <= 2)
      {
        myOSystem.frameBuffer().showMessage("Phosphor blend at minimum");
        return;
      }
      else
        blend = std::max(blend-2, 0);
    }
    else
      return;

    ostringstream val;
    val << blend;
    myProperties.set(Display_PPBlend, val.str());
    myOSystem.frameBuffer().showMessage("Phosphor blend " + val.str());
    myOSystem.frameBuffer().tiaSurface().enablePhosphor(true, blend);
  }
  else
    myOSystem.frameBuffer().showMessage("Phosphor effect disabled");
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::setProperties(const Properties& props)
{
  myProperties = props;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
FBInitStatus Console::initializeVideo(bool full)
{
  FBInitStatus fbstatus = kSuccess;

  if(full)
  {
    const string& title = string("Stella ") + STELLA_VERSION +
                   ": \"" + myProperties.get(Cartridge_Name) + "\"";
    fbstatus = myOSystem.frameBuffer().createDisplay(title,
                 myTIA->width() << 1, myTIA->height());
    if(fbstatus != kSuccess)
      return fbstatus;

    myOSystem.frameBuffer().showFrameStats(myOSystem.settings().getBool("stats"));
    generateColorLossPalette();
  }
  setPalette(myOSystem.settings().getString("palette"));

  // Set the correct framerate based on the format of the ROM
  // This can be overridden by changing the framerate in the
  // VideoDialog box or on the commandline, but it can't be saved
  // (ie, framerate is now determined based on number of scanlines).
  int framerate = myOSystem.settings().getInt("framerate");
  if(framerate > 0) myFramerate = float(framerate);
  myOSystem.setFramerate(myFramerate);

  // Make sure auto-frame calculation is only enabled when necessary
  myTIA->enableAutoFrame(framerate <= 0);

  return fbstatus;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::initializeAudio()
{
  // Initialize the sound interface.
  // The # of channels can be overridden in the AudioDialog box or on
  // the commandline, but it can't be saved.
  int framerate = myOSystem.settings().getInt("framerate");
  if(framerate > 0) myFramerate = float(framerate);
  const string& sound = myProperties.get(Cartridge_Sound);

  myOSystem.sound().close();
  myOSystem.sound().setChannels(sound == "STEREO" ? 2 : 1);
  myOSystem.sound().setFrameRate(myFramerate);
  myOSystem.sound().open();

  // Make sure auto-frame calculation is only enabled when necessary
  myTIA->enableAutoFrame(framerate <= 0);
}

/* Original frying research and code by Fred Quimby.
   I've tried the following variations on this code:
   - Both OR and Exclusive OR instead of AND. This generally crashes the game
     without ever giving us realistic "fried" effects.
   - Loop only over the RIOT RAM. This still gave us frying-like effects, but
     it seemed harder to duplicate most effects. I have no idea why, but
     munging the TIA regs seems to have some effect (I'd think it wouldn't).

   Fred says he also tried mangling the PC and registers, but usually it'd just
   crash the game (e.g. black screen, no way out of it).

   It's definitely easier to get some effects (e.g. 255 lives in Battlezone)
   with this code than it is on a real console. My guess is that most "good"
   frying effects come from a RIOT location getting cleared to 0. Fred's
   code is more likely to accomplish this than frying a real console is...

   Until someone comes up with a more accurate way to emulate frying, I'm
   leaving this as Fred posted it.   -- B.
*/
// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::fry() const
{
  for(int i = 0; i < 0x100; i += mySystem->randGenerator().next() % 4)
    mySystem->poke(i, mySystem->peek(i) & mySystem->randGenerator().next());
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::changeYStart(int direction)
{
  uInt32 ystart = myTIA->ystart();

  if(direction == +1)       // increase YStart
  {
    if(ystart >= FrameManager::maxYStart)
    {
      myOSystem.frameBuffer().showMessage("YStart at maximum");
      return;
    }
    ystart++;
  }
  else if(direction == -1)  // decrease YStart
  {
    if(ystart == FrameManager::minYStart-1)
    {
      myOSystem.frameBuffer().showMessage("YStart at minimum");
      return;
    }
    ystart--;
  }
  else
    return;

  ostringstream val;
  val << ystart;
  if(ystart == FrameManager::minYStart-1)
    myOSystem.frameBuffer().showMessage("YStart autodetected");
  else
  {
    if(myTIA->ystartIsAuto(ystart))
    {
      // We've reached the auto-detect value, so reset
      myOSystem.frameBuffer().showMessage("YStart " + val.str() + " (Auto)");
      val.str("");
      val << FrameManager::minYStart-1;
    }
    else
      myOSystem.frameBuffer().showMessage("YStart " + val.str());
  }

  myProperties.set(Display_YStart, val.str());
  myTIA->setYStart(ystart);
  myTIA->frameReset();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::changeHeight(int direction)
{
  uInt32 height = myTIA->height();
  uInt32 dheight = myOSystem.frameBuffer().desktopSize().h;

  if(direction == +1)       // increase Height
  {
    height++;
    if(height > FrameManager::maxViewableHeight || height > dheight)
    {
      myOSystem.frameBuffer().showMessage("Height at maximum");
      return;
    }
  }
  else if(direction == -1)  // decrease Height
  {
    height--;
    if(height < FrameManager::minViewableHeight) height = 0;
  }
  else
    return;

  myTIA->setHeight(height);
  myTIA->frameReset();
  initializeVideo();  // takes care of refreshing the screen

  ostringstream val;
  val << height;
  myOSystem.frameBuffer().showMessage("Height " + val.str());
  myProperties.set(Display_Height, val.str());
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::setTIAProperties()
{
  uInt32 ystart = atoi(myProperties.get(Display_YStart).c_str());
  if(ystart != 0)
    ystart = BSPF::clamp(ystart, FrameManager::minYStart, FrameManager::maxYStart);
  uInt32 height = atoi(myProperties.get(Display_Height).c_str());
  if(height != 0)
    height = BSPF::clamp(height, FrameManager::minViewableHeight, FrameManager::maxViewableHeight);

  myTIA->autodetectLayout(false);

  if(myDisplayFormat == "NTSC" || myDisplayFormat == "PAL60" ||
     myDisplayFormat == "SECAM60")
  {
    // Assume we've got ~262 scanlines (NTSC-like format)
    myFramerate = 60.0;
    myConsoleInfo.InitialFrameRate = "60";
    myTIA->setLayout(FrameLayout::ntsc);
  }
  else
  {
    // Assume we've got ~312 scanlines (PAL-like format)
    myFramerate = 50.0;
    myConsoleInfo.InitialFrameRate = "50";

    // PAL ROMs normally need at least 250 lines
    if (height != 0) height = std::max(height, 250u);

    myTIA->setLayout(FrameLayout::pal);
  }

  myTIA->setYStart(ystart);
  myTIA->setHeight(height);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::setControllers(const string& rommd5)
{
  // Setup the controllers based on properties
  const string& left  = myProperties.get(Controller_Left);
  const string& right = myProperties.get(Controller_Right);

  // Check for CompuMate controllers; they are special in that a handler
  // creates them for us, and also that they must be used in both ports
  if(left == "COMPUMATE" || right == "COMPUMATE")
  {
    myCMHandler = make_shared<CompuMate>(*this, myEvent, *mySystem);

    // A somewhat ugly bit of code that casts to CartridgeCM to
    // add the CompuMate, and then back again for the actual
    // Cartridge
    unique_ptr<CartridgeCM> cartcm(static_cast<CartridgeCM*>(myCart.release()));
    cartcm->setCompuMate(myCMHandler);
    myCart = std::move(cartcm);

    myLeftControl  = std::move(myCMHandler->leftController());
    myRightControl = std::move(myCMHandler->rightController());
    return;
  }

  unique_ptr<Controller> leftC  = std::move(myLeftControl),
                         rightC = std::move(myRightControl);

  // Also check if we should swap the paddles plugged into a jack
  bool swapPaddles = myProperties.get(Controller_SwapPaddles) == "YES";

  // Construct left controller
  if(left == "JOYSTICK")
  {
    // Already created in c'tor
    // We save some time by not looking at all the other types
    if(!leftC)
      leftC = make_unique<Joystick>(Controller::Left, myEvent, *mySystem);
  }
  else if(left == "BOOSTERGRIP")
  {
    leftC = make_unique<BoosterGrip>(Controller::Left, myEvent, *mySystem);
  }
  else if(left == "DRIVING")
  {
    leftC = make_unique<Driving>(Controller::Left, myEvent, *mySystem);
  }
  else if((left == "KEYBOARD") || (left == "KEYPAD"))
  {
    leftC = make_unique<Keyboard>(Controller::Left, myEvent, *mySystem);
  }
  else if(BSPF::startsWithIgnoreCase(left, "PADDLES"))
  {
    bool swapAxis = false, swapDir = false;
    if(left == "PADDLES_IAXIS")
      swapAxis = true;
    else if(left == "PADDLES_IDIR")
      swapDir = true;
    else if(left == "PADDLES_IAXDR")
      swapAxis = swapDir = true;
    leftC = make_unique<Paddles>(Controller::Left, myEvent, *mySystem,
                              swapPaddles, swapAxis, swapDir);
  }
  else if(left == "AMIGAMOUSE")
  {
    leftC = make_unique<AmigaMouse>(Controller::Left, myEvent, *mySystem);
  }
  else if(left == "ATARIMOUSE")
  {
    leftC = make_unique<AtariMouse>(Controller::Left, myEvent, *mySystem);
  }
  else if(left == "TRAKBALL")
  {
    leftC = make_unique<TrakBall>(Controller::Left, myEvent, *mySystem);
  }
  else if(left == "GENESIS")
  {
    leftC = make_unique<Genesis>(Controller::Left, myEvent, *mySystem);
  }
  else if(left == "MINDLINK")
  {
    leftC = make_unique<MindLink>(Controller::Left, myEvent, *mySystem);
  }

  // Construct right controller
  if(right == "JOYSTICK")
  {
    // Already created in c'tor
    // We save some time by not looking at all the other types
    if(!rightC)
      rightC = make_unique<Joystick>(Controller::Right, myEvent, *mySystem);
  }
  else if(right == "BOOSTERGRIP")
  {
    rightC = make_unique<BoosterGrip>(Controller::Right, myEvent, *mySystem);
  }
  else if(right == "DRIVING")
  {
    rightC = make_unique<Driving>(Controller::Right, myEvent, *mySystem);
  }
  else if((right == "KEYBOARD") || (right == "KEYPAD"))
  {
    rightC = make_unique<Keyboard>(Controller::Right, myEvent, *mySystem);
  }
  else if(BSPF::startsWithIgnoreCase(right, "PADDLES"))
  {
    bool swapAxis = false, swapDir = false;
    if(right == "PADDLES_IAXIS")
      swapAxis = true;
    else if(right == "PADDLES_IDIR")
      swapDir = true;
    else if(right == "PADDLES_IAXDR")
      swapAxis = swapDir = true;
    rightC = make_unique<Paddles>(Controller::Right, myEvent, *mySystem,
                               swapPaddles, swapAxis, swapDir);
  }
  else if(left == "AMIGAMOUSE")
  {
    rightC = make_unique<AmigaMouse>(Controller::Left, myEvent, *mySystem);
  }
  else if(left == "ATARIMOUSE")
  {
    rightC = make_unique<AtariMouse>(Controller::Left, myEvent, *mySystem);
  }
  else if(left == "TRAKBALL")
  {
    rightC = make_unique<TrakBall>(Controller::Left, myEvent, *mySystem);
  }
  else if(right == "ATARIVOX")
  {
    const string& nvramfile = myOSystem.nvramDir() + "atarivox_eeprom.dat";
    rightC = make_unique<AtariVox>(Controller::Right, myEvent,
                   *mySystem, myOSystem.serialPort(),
                   myOSystem.settings().getString("avoxport"), nvramfile);
  }
  else if(right == "SAVEKEY")
  {
    const string& nvramfile = myOSystem.nvramDir() + "savekey_eeprom.dat";
    rightC = make_unique<SaveKey>(Controller::Right, myEvent, *mySystem,
                               nvramfile);
  }
  else if(right == "GENESIS")
  {
    rightC = make_unique<Genesis>(Controller::Right, myEvent, *mySystem);
  }
  else if(right == "KIDVID")
  {
    rightC = make_unique<KidVid>(Controller::Right, myEvent, *mySystem, rommd5);
  }
  else if(right == "MINDLINK")
  {
    rightC = make_unique<MindLink>(Controller::Right, myEvent, *mySystem);
  }

  // Swap the ports if necessary
  if(myProperties.get(Console_SwapPorts) == "NO")
  {
    myLeftControl  = std::move(leftC);
    myRightControl = std::move(rightC);
  }
  else
  {
    myLeftControl  = std::move(rightC);
    myRightControl = std::move(leftC);
  }

  myTIA->bindToControllers();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::loadUserPalette()
{
  const string& palette = myOSystem.paletteFile();
  ifstream in(palette, std::ios::binary);
  if(!in)
    return;

  // Make sure the contains enough data for the NTSC, PAL and SECAM palettes
  // This means 128 colours each for NTSC and PAL, at 3 bytes per pixel
  // and 8 colours for SECAM at 3 bytes per pixel
  in.seekg(0, std::ios::end);
  std::streampos length = in.tellg();
  in.seekg(0, std::ios::beg);
  if(length < 128 * 3 * 2 + 8 * 3)
  {
    cerr << "ERROR: invalid palette file " << palette << endl;
    return;
  }

  // Now that we have valid data, create the user-defined palettes
  uInt8 pixbuf[3];  // Temporary buffer for one 24-bit pixel

  for(int i = 0; i < 128; i++)  // NTSC palette
  {
    in.read(reinterpret_cast<char*>(pixbuf), 3);
    uInt32 pixel = (int(pixbuf[0]) << 16) + (int(pixbuf[1]) << 8) + int(pixbuf[2]);
    ourUserNTSCPalette[(i<<1)] = pixel;
  }
  for(int i = 0; i < 128; i++)  // PAL palette
  {
    in.read(reinterpret_cast<char*>(pixbuf), 3);
    uInt32 pixel = (int(pixbuf[0]) << 16) + (int(pixbuf[1]) << 8) + int(pixbuf[2]);
    ourUserPALPalette[(i<<1)] = pixel;
  }

  uInt32 secam[16];  // All 8 24-bit pixels, plus 8 colorloss pixels
  for(int i = 0; i < 8; i++)    // SECAM palette
  {
    in.read(reinterpret_cast<char*>(pixbuf), 3);
    uInt32 pixel = (int(pixbuf[0]) << 16) + (int(pixbuf[1]) << 8) + int(pixbuf[2]);
    secam[(i<<1)]   = pixel;
    secam[(i<<1)+1] = 0;
  }
  uInt32* ptr = ourUserSECAMPalette;
  for(int i = 0; i < 16; ++i)
  {
    uInt32* s = secam;
    for(int j = 0; j < 16; ++j)
      *ptr++ = *s++;
  }

  myUserPaletteDefined = true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::generateColorLossPalette()
{
  // Look at all the palettes, since we don't know which one is
  // currently active
  uInt32* palette[9] = {
    &ourNTSCPalette[0],    &ourPALPalette[0],    &ourSECAMPalette[0],
    &ourNTSCPaletteZ26[0], &ourPALPaletteZ26[0], &ourSECAMPaletteZ26[0],
    0, 0, 0
  };
  if(myUserPaletteDefined)
  {
    palette[6] = &ourUserNTSCPalette[0];
    palette[7] = &ourUserPALPalette[0];
    palette[8] = &ourUserSECAMPalette[0];
  }

  for(int i = 0; i < 9; ++i)
  {
    if(palette[i] == 0)
      continue;

    // Fill the odd numbered palette entries with gray values (calculated
    // using the standard RGB -> grayscale conversion formula)
    for(int j = 0; j < 128; ++j)
    {
      uInt32 pixel = palette[i][(j<<1)];
      uInt8 r = (pixel >> 16) & 0xff;
      uInt8 g = (pixel >> 8)  & 0xff;
      uInt8 b = (pixel >> 0)  & 0xff;
      uInt8 sum = uInt8((r * 0.2989) + (g * 0.5870) + (b * 0.1140));
      palette[i][(j<<1)+1] = (sum << 16) + (sum << 8) + sum;
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::setFramerate(float framerate)
{
  myFramerate = framerate;
  myOSystem.setFramerate(framerate);
  myOSystem.sound().setFrameRate(framerate);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::toggleTIABit(TIABit bit, const string& bitname, bool show) const
{
  bool result = myTIA->toggleBit(bit);
  string message = bitname + (result ? " enabled" : " disabled");
  myOSystem.frameBuffer().showMessage(message);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::toggleBits() const
{
  bool enabled = myTIA->toggleBits();
  string message = string("TIA bits") + (enabled ? " enabled" : " disabled");
  myOSystem.frameBuffer().showMessage(message);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::toggleTIACollision(TIABit bit, const string& bitname, bool show) const
{
  bool result = myTIA->toggleCollision(bit);
  string message = bitname + (result ? " collision enabled" : " collision disabled");
  myOSystem.frameBuffer().showMessage(message);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::toggleCollisions() const
{
  bool enabled = myTIA->toggleCollisions();
  string message = string("TIA collisions") + (enabled ? " enabled" : " disabled");
  myOSystem.frameBuffer().showMessage(message);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::toggleFixedColors() const
{
  if(myTIA->toggleFixedColors())
    myOSystem.frameBuffer().showMessage("Fixed debug colors enabled");
  else
    myOSystem.frameBuffer().showMessage("Fixed debug colors disabled");
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::toggleJitter() const
{
  bool enabled = myTIA->toggleJitter();
  string message = string("TV scanline jitter") + (enabled ? " enabled" : " disabled");
  myOSystem.frameBuffer().showMessage(message);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::attachDebugger(Debugger& dbg)
{
#ifdef DEBUGGER_SUPPORT
//  myOSystem.createDebugger(*this);
  mySystem->m6502().attach(dbg);
#endif
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Console::stateChanged(EventHandler::State state)
{
  // For now, only the CompuMate cares about state changes
  if(myCMHandler)
    myCMHandler->enableKeyHandling(state == EventHandler::S_EMULATE);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 Console::ourNTSCPalette[256] = {
  0x000000, 0, 0x4a4a4a, 0, 0x6f6f6f, 0, 0x8e8e8e, 0,
  0xaaaaaa, 0, 0xc0c0c0, 0, 0xd6d6d6, 0, 0xececec, 0,
  0x484800, 0, 0x69690f, 0, 0x86861d, 0, 0xa2a22a, 0,
  0xbbbb35, 0, 0xd2d240, 0, 0xe8e84a, 0, 0xfcfc54, 0,
  0x7c2c00, 0, 0x904811, 0, 0xa26221, 0, 0xb47a30, 0,
  0xc3903d, 0, 0xd2a44a, 0, 0xdfb755, 0, 0xecc860, 0,
  0x901c00, 0, 0xa33915, 0, 0xb55328, 0, 0xc66c3a, 0,
  0xd5824a, 0, 0xe39759, 0, 0xf0aa67, 0, 0xfcbc74, 0,
  0x940000, 0, 0xa71a1a, 0, 0xb83232, 0, 0xc84848, 0,
  0xd65c5c, 0, 0xe46f6f, 0, 0xf08080, 0, 0xfc9090, 0,
  0x840064, 0, 0x97197a, 0, 0xa8308f, 0, 0xb846a2, 0,
  0xc659b3, 0, 0xd46cc3, 0, 0xe07cd2, 0, 0xec8ce0, 0,
  0x500084, 0, 0x68199a, 0, 0x7d30ad, 0, 0x9246c0, 0,
  0xa459d0, 0, 0xb56ce0, 0, 0xc57cee, 0, 0xd48cfc, 0,
  0x140090, 0, 0x331aa3, 0, 0x4e32b5, 0, 0x6848c6, 0,
  0x7f5cd5, 0, 0x956fe3, 0, 0xa980f0, 0, 0xbc90fc, 0,
  0x000094, 0, 0x181aa7, 0, 0x2d32b8, 0, 0x4248c8, 0,
  0x545cd6, 0, 0x656fe4, 0, 0x7580f0, 0, 0x8490fc, 0,
  0x001c88, 0, 0x183b9d, 0, 0x2d57b0, 0, 0x4272c2, 0,
  0x548ad2, 0, 0x65a0e1, 0, 0x75b5ef, 0, 0x84c8fc, 0,
  0x003064, 0, 0x185080, 0, 0x2d6d98, 0, 0x4288b0, 0,
  0x54a0c5, 0, 0x65b7d9, 0, 0x75cceb, 0, 0x84e0fc, 0,
  0x004030, 0, 0x18624e, 0, 0x2d8169, 0, 0x429e82, 0,
  0x54b899, 0, 0x65d1ae, 0, 0x75e7c2, 0, 0x84fcd4, 0,
  0x004400, 0, 0x1a661a, 0, 0x328432, 0, 0x48a048, 0,
  0x5cba5c, 0, 0x6fd26f, 0, 0x80e880, 0, 0x90fc90, 0,
  0x143c00, 0, 0x355f18, 0, 0x527e2d, 0, 0x6e9c42, 0,
  0x87b754, 0, 0x9ed065, 0, 0xb4e775, 0, 0xc8fc84, 0,
  0x303800, 0, 0x505916, 0, 0x6d762b, 0, 0x88923e, 0,
  0xa0ab4f, 0, 0xb7c25f, 0, 0xccd86e, 0, 0xe0ec7c, 0,
  0x482c00, 0, 0x694d14, 0, 0x866a26, 0, 0xa28638, 0,
  0xbb9f47, 0, 0xd2b656, 0, 0xe8cc63, 0, 0xfce070, 0
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 Console::ourPALPalette[256] = {
  0x000000, 0, 0x2b2b2b, 0, 0x525252, 0, 0x767676, 0,
  0x979797, 0, 0xb6b6b6, 0, 0xd2d2d2, 0, 0xececec, 0,
  0x000000, 0, 0x2b2b2b, 0, 0x525252, 0, 0x767676, 0,
  0x979797, 0, 0xb6b6b6, 0, 0xd2d2d2, 0, 0xececec, 0,
  0x805800, 0, 0x96711a, 0, 0xab8732, 0, 0xbe9c48, 0,
  0xcfaf5c, 0, 0xdfc06f, 0, 0xeed180, 0, 0xfce090, 0,
  0x445c00, 0, 0x5e791a, 0, 0x769332, 0, 0x8cac48, 0,
  0xa0c25c, 0, 0xb3d76f, 0, 0xc4ea80, 0, 0xd4fc90, 0,
  0x703400, 0, 0x89511a, 0, 0xa06b32, 0, 0xb68448, 0,
  0xc99a5c, 0, 0xdcaf6f, 0, 0xecc280, 0, 0xfcd490, 0,
  0x006414, 0, 0x1a8035, 0, 0x329852, 0, 0x48b06e, 0,
  0x5cc587, 0, 0x6fd99e, 0, 0x80ebb4, 0, 0x90fcc8, 0,
  0x700014, 0, 0x891a35, 0, 0xa03252, 0, 0xb6486e, 0,
  0xc95c87, 0, 0xdc6f9e, 0, 0xec80b4, 0, 0xfc90c8, 0,
  0x005c5c, 0, 0x1a7676, 0, 0x328e8e, 0, 0x48a4a4, 0,
  0x5cb8b8, 0, 0x6fcbcb, 0, 0x80dcdc, 0, 0x90ecec, 0,
  0x70005c, 0, 0x841a74, 0, 0x963289, 0, 0xa8489e, 0,
  0xb75cb0, 0, 0xc66fc1, 0, 0xd380d1, 0, 0xe090e0, 0,
  0x003c70, 0, 0x195a89, 0, 0x2f75a0, 0, 0x448eb6, 0,
  0x57a5c9, 0, 0x68badc, 0, 0x79ceec, 0, 0x88e0fc, 0,
  0x580070, 0, 0x6e1a89, 0, 0x8332a0, 0, 0x9648b6, 0,
  0xa75cc9, 0, 0xb76fdc, 0, 0xc680ec, 0, 0xd490fc, 0,
  0x002070, 0, 0x193f89, 0, 0x2f5aa0, 0, 0x4474b6, 0,
  0x578bc9, 0, 0x68a1dc, 0, 0x79b5ec, 0, 0x88c8fc, 0,
  0x340080, 0, 0x4a1a96, 0, 0x5f32ab, 0, 0x7248be, 0,
  0x835ccf, 0, 0x936fdf, 0, 0xa280ee, 0, 0xb090fc, 0,
  0x000088, 0, 0x1a1a9d, 0, 0x3232b0, 0, 0x4848c2, 0,
  0x5c5cd2, 0, 0x6f6fe1, 0, 0x8080ef, 0, 0x9090fc, 0,
  0x000000, 0, 0x2b2b2b, 0, 0x525252, 0, 0x767676, 0,
  0x979797, 0, 0xb6b6b6, 0, 0xd2d2d2, 0, 0xececec, 0,
  0x000000, 0, 0x2b2b2b, 0, 0x525252, 0, 0x767676, 0,
  0x979797, 0, 0xb6b6b6, 0, 0xd2d2d2, 0, 0xececec, 0
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 Console::ourSECAMPalette[256] = {
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff50ff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 Console::ourNTSCPaletteZ26[256] = {
  0x000000, 0, 0x505050, 0, 0x646464, 0, 0x787878, 0,
  0x8c8c8c, 0, 0xa0a0a0, 0, 0xb4b4b4, 0, 0xc8c8c8, 0,
  0x445400, 0, 0x586800, 0, 0x6c7c00, 0, 0x809000, 0,
  0x94a414, 0, 0xa8b828, 0, 0xbccc3c, 0, 0xd0e050, 0,
  0x673900, 0, 0x7b4d00, 0, 0x8f6100, 0, 0xa37513, 0,
  0xb78927, 0, 0xcb9d3b, 0, 0xdfb14f, 0, 0xf3c563, 0,
  0x7b2504, 0, 0x8f3918, 0, 0xa34d2c, 0, 0xb76140, 0,
  0xcb7554, 0, 0xdf8968, 0, 0xf39d7c, 0, 0xffb190, 0,
  0x7d122c, 0, 0x912640, 0, 0xa53a54, 0, 0xb94e68, 0,
  0xcd627c, 0, 0xe17690, 0, 0xf58aa4, 0, 0xff9eb8, 0,
  0x730871, 0, 0x871c85, 0, 0x9b3099, 0, 0xaf44ad, 0,
  0xc358c1, 0, 0xd76cd5, 0, 0xeb80e9, 0, 0xff94fd, 0,
  0x5d0b92, 0, 0x711fa6, 0, 0x8533ba, 0, 0x9947ce, 0,
  0xad5be2, 0, 0xc16ff6, 0, 0xd583ff, 0, 0xe997ff, 0,
  0x401599, 0, 0x5429ad, 0, 0x683dc1, 0, 0x7c51d5, 0,
  0x9065e9, 0, 0xa479fd, 0, 0xb88dff, 0, 0xcca1ff, 0,
  0x252593, 0, 0x3939a7, 0, 0x4d4dbb, 0, 0x6161cf, 0,
  0x7575e3, 0, 0x8989f7, 0, 0x9d9dff, 0, 0xb1b1ff, 0,
  0x0f3480, 0, 0x234894, 0, 0x375ca8, 0, 0x4b70bc, 0,
  0x5f84d0, 0, 0x7398e4, 0, 0x87acf8, 0, 0x9bc0ff, 0,
  0x04425a, 0, 0x18566e, 0, 0x2c6a82, 0, 0x407e96, 0,
  0x5492aa, 0, 0x68a6be, 0, 0x7cbad2, 0, 0x90cee6, 0,
  0x044f30, 0, 0x186344, 0, 0x2c7758, 0, 0x408b6c, 0,
  0x549f80, 0, 0x68b394, 0, 0x7cc7a8, 0, 0x90dbbc, 0,
  0x0f550a, 0, 0x23691e, 0, 0x377d32, 0, 0x4b9146, 0,
  0x5fa55a, 0, 0x73b96e, 0, 0x87cd82, 0, 0x9be196, 0,
  0x1f5100, 0, 0x336505, 0, 0x477919, 0, 0x5b8d2d, 0,
  0x6fa141, 0, 0x83b555, 0, 0x97c969, 0, 0xabdd7d, 0,
  0x344600, 0, 0x485a00, 0, 0x5c6e14, 0, 0x708228, 0,
  0x84963c, 0, 0x98aa50, 0, 0xacbe64, 0, 0xc0d278, 0,
  0x463e00, 0, 0x5a5205, 0, 0x6e6619, 0, 0x827a2d, 0,
  0x968e41, 0, 0xaaa255, 0, 0xbeb669, 0, 0xd2ca7d, 0
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 Console::ourPALPaletteZ26[256] = {
  0x000000, 0, 0x4c4c4c, 0, 0x606060, 0, 0x747474, 0,
  0x888888, 0, 0x9c9c9c, 0, 0xb0b0b0, 0, 0xc4c4c4, 0,
  0x000000, 0, 0x4c4c4c, 0, 0x606060, 0, 0x747474, 0,
  0x888888, 0, 0x9c9c9c, 0, 0xb0b0b0, 0, 0xc4c4c4, 0,
  0x533a00, 0, 0x674e00, 0, 0x7b6203, 0, 0x8f7617, 0,
  0xa38a2b, 0, 0xb79e3f, 0, 0xcbb253, 0, 0xdfc667, 0,
  0x1b5800, 0, 0x2f6c00, 0, 0x438001, 0, 0x579415, 0,
  0x6ba829, 0, 0x7fbc3d, 0, 0x93d051, 0, 0xa7e465, 0,
  0x6a2900, 0, 0x7e3d12, 0, 0x925126, 0, 0xa6653a, 0,
  0xba794e, 0, 0xce8d62, 0, 0xe2a176, 0, 0xf6b58a, 0,
  0x075b00, 0, 0x1b6f11, 0, 0x2f8325, 0, 0x439739, 0,
  0x57ab4d, 0, 0x6bbf61, 0, 0x7fd375, 0, 0x93e789, 0,
  0x741b2f, 0, 0x882f43, 0, 0x9c4357, 0, 0xb0576b, 0,
  0xc46b7f, 0, 0xd87f93, 0, 0xec93a7, 0, 0xffa7bb, 0,
  0x00572e, 0, 0x106b42, 0, 0x247f56, 0, 0x38936a, 0,
  0x4ca77e, 0, 0x60bb92, 0, 0x74cfa6, 0, 0x88e3ba, 0,
  0x6d165f, 0, 0x812a73, 0, 0x953e87, 0, 0xa9529b, 0,
  0xbd66af, 0, 0xd17ac3, 0, 0xe58ed7, 0, 0xf9a2eb, 0,
  0x014c5e, 0, 0x156072, 0, 0x297486, 0, 0x3d889a, 0,
  0x519cae, 0, 0x65b0c2, 0, 0x79c4d6, 0, 0x8dd8ea, 0,
  0x5f1588, 0, 0x73299c, 0, 0x873db0, 0, 0x9b51c4, 0,
  0xaf65d8, 0, 0xc379ec, 0, 0xd78dff, 0, 0xeba1ff, 0,
  0x123b87, 0, 0x264f9b, 0, 0x3a63af, 0, 0x4e77c3, 0,
  0x628bd7, 0, 0x769feb, 0, 0x8ab3ff, 0, 0x9ec7ff, 0,
  0x451e9d, 0, 0x5932b1, 0, 0x6d46c5, 0, 0x815ad9, 0,
  0x956eed, 0, 0xa982ff, 0, 0xbd96ff, 0, 0xd1aaff, 0,
  0x2a2b9e, 0, 0x3e3fb2, 0, 0x5253c6, 0, 0x6667da, 0,
  0x7a7bee, 0, 0x8e8fff, 0, 0xa2a3ff, 0, 0xb6b7ff, 0,
  0x000000, 0, 0x4c4c4c, 0, 0x606060, 0, 0x747474, 0,
  0x888888, 0, 0x9c9c9c, 0, 0xb0b0b0, 0, 0xc4c4c4, 0,
  0x000000, 0, 0x4c4c4c, 0, 0x606060, 0, 0x747474, 0,
  0x888888, 0, 0x9c9c9c, 0, 0xb0b0b0, 0, 0xc4c4c4, 0
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 Console::ourSECAMPaletteZ26[256] = {
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0,
  0x000000, 0, 0x2121ff, 0, 0xf03c79, 0, 0xff3cff, 0,
  0x7fff00, 0, 0x7fffff, 0, 0xffff3f, 0, 0xffffff, 0
};

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 Console::ourUserNTSCPalette[256]  = { 0 }; // filled from external file

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 Console::ourUserPALPalette[256]   = { 0 }; // filled from external file

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt32 Console::ourUserSECAMPalette[256] = { 0 }; // filled from external file
