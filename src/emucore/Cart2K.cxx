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

#include "System.hxx"
#include "Cart2K.hxx"

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
Cartridge2K::Cartridge2K(const BytePtr& image, uInt32 size,
                         const Settings& settings)
  : Cartridge(settings)
{
  // Size can be a maximum of 2K
  if(size > 2048) size = 2048;

  // Set image size to closest power-of-two for the given size
  mySize = 1;
  while(mySize < size)
    mySize <<= 1;

  // The smallest addressable area by Stella is 64 bytes
  // This should really be read from the System, but for now I'm going
  // to cheat a little and hard-code it to 64 (aka 2^6)
  if(mySize < 64)
    mySize = 64;

  // Initialize ROM with illegal 6502 opcode that causes a real 6502 to jam
  myImage = make_unique<uInt8[]>(mySize);
  memset(myImage.get(), 0x02, mySize);

  // Copy the ROM image into my buffer
  memcpy(myImage.get(), image.get(), size);
  createCodeAccessBase(mySize);

  // Set mask for accessing the image buffer
  // This is guaranteed to work, as mySize is a power of two
  myMask = mySize - 1;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Cartridge2K::reset()
{
  myBankChanged = true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void Cartridge2K::install(System& system)
{
  mySystem = &system;

  // Map ROM image into the system
  System::PageAccess access(this, System::PA_READ);
  for(uInt32 address = 0x1000; address < 0x2000; address += (1 << System::PAGE_SHIFT))
  {
    access.directPeekBase = &myImage[address & myMask];
    access.codeAccessBase = &myCodeAccessBase[address & myMask];
    mySystem->setPageAccess(address >> System::PAGE_SHIFT, access);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
uInt8 Cartridge2K::peek(uInt16 address)
{
  return myImage[address & myMask];
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool Cartridge2K::poke(uInt16, uInt8)
{
  // This is ROM so poking has no effect :-)
  return false;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool Cartridge2K::patch(uInt16 address, uInt8 value)
{
  myImage[address & myMask] = value;
  return myBankChanged = true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
const uInt8* Cartridge2K::getImage(int& size) const
{
  size = mySize;
  return myImage.get();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool Cartridge2K::save(Serializer& out) const
{
  try
  {
    out.putString(name());
  }
  catch(...)
  {
    cerr << "ERROR: Cartridge2K::save" << endl;
    return false;
  }

  return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool Cartridge2K::load(Serializer& in)
{
  try
  {
    if(in.getString() != name())
      return false;
  }
  catch(...)
  {
    cerr << "ERROR: Cartridge2K::load" << endl;
    return false;
  }

  return true;
}
