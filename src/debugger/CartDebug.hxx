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
// Copyright (c) 1995-2009 by Bradford W. Mott and the Stella team
//
// See the file "license" for information on usage and redistribution of
// this file, and for a DISCLAIMER OF ALL WARRANTIES.
//
// $Id$
//============================================================================

#ifndef CART_DEBUG_HXX
#define CART_DEBUG_HXX

class System;

#include "bspf.hxx"
#include "Array.hxx"
#include "Cart.hxx"
#include "DebuggerSystem.hxx"

// pointer types for CartDebug instance methods
typedef int (CartDebug::*CARTDEBUG_INT_METHOD)();

// call the pointed-to method on the (global) CPU debugger object.
#define CALL_CARTDEBUG_METHOD(method) ( ( Debugger::debugger().cartDebug().*method)() )

class CartState : public DebuggerState
{
  public:
    IntArray ram;    // The actual data values
    IntArray rport;  // Address for reading from RAM
    IntArray wport;  // Address for writing to RAM
};

class CartDebug : public DebuggerSystem
{
  public:
    struct DisassemblyTag {
      uInt16 address;
      string label;
      string disasm;
      string bytes;
    };
    typedef Common::Array<DisassemblyTag> DisassemblyList;

  public:
    CartDebug(Debugger& dbg, Console& console, const RamAreaList& areas);

    const DebuggerState& getState();
    const DebuggerState& getOldState() { return myOldState; }

    void saveOldState();
    string toString();

    // The following assume that the given addresses are using the
    // correct read/write port ranges; no checking will be done to
    // confirm this.
    uInt8 read(uInt16 addr);
    void write(uInt16 addr, uInt8 value);

    // Return the address at which an invalid read was performed in a
    // write port area.
    int readFromWritePort();

    // Indicate that a read from write port has occurred.
    void triggerReadFromWritePort(uInt16 addr) { myRWPortAddress = addr; }

    /**
      Let the Cart debugger subsystem treat this area as addressable memory.

      @param start    The beginning of the RAM area (0x0000 - 0x2000)
      @param size     Total number of bytes of area
      @param roffset  Offset to use when reading from RAM (read port)
      @param woffset  Offset to use when writing to RAM (write port)
    */
    void addRamArea(uInt16 start, uInt16 size, uInt16 roffset, uInt16 woffset);

////////////////////////////////////////
    /**
      Disassemble from the starting address the specified number of lines
      and place result in a string.
    */
    const string& disassemble(int start, int lines);

    /**
      Disassemble from the starting address to the ending address
      and place addresses, bytes and data in given arrays.
    */
    void disassemble(IntArray& addr, StringList& addrLabel,
                     StringList& bytes, StringList& data,
                     int start, int end);

    /**
      Disassemble from the starting address, placing results into a
      DisassemblyList.
    */
    void disassemble(DisassemblyList& list, uInt16 start);

    int getBank();
    int bankCount();
    string getCartType();
////////////////////////////////////////

    /**
      Add a label and associated address
    */
    void addLabel(const string& label, uInt16 address);

    /**
      Remove the given label and its associated address
    */
    bool removeLabel(const string& label);

    /**
      Accessor methods for labels and addresses

      The mapping from address to label can be one-to-many (ie, an
      address can have different labels depending on its context, and
      whether its being read or written; if isRead is true, the context
      is a read, else it's a write
      If places is not -1 and a label hasn't been defined, return a
      formatted hexidecimal address
    */
    const string& getLabel(uInt16 addr, bool isRead, int places = -1) const;
    int getAddress(const string& label) const;

    /**
      Load user equates from the given symbol file (generated by DASM)
    */
    string loadSymbolFile(const string& file);

    /**
      Save user equates into a symbol file similar to that generated by DASM
    */
    bool saveSymbolFile(const string& file);

    /**
      Methods used by the command parser for tab-completion
    */
    int countCompletions(const char *in);
    const string& getCompletions() const      { return myCompletions; }
    const string& getCompletionPrefix() const { return myCompPrefix;  }

  private:
    int disassemble(int address, string& result);

  private:
    enum equate_t {
      EQF_READ  = 1 << 0,               // address can be read from
      EQF_WRITE = 1 << 1,               // address can be written to
      EQF_RW    = EQF_READ | EQF_WRITE  // address can be both read and written
    };
    enum address_t {
      ADDR_TIA  = 1 << 0,
      ADDR_RAM  = 1 << 1,
      ADDR_RIOT = 1 << 2,
      ADDR_ROM  = 1 << 3
    };
    struct Equate {
      string label;
      uInt16 address;
      equate_t flags;
    };

    typedef map<uInt16, Equate> AddrToLabel;
    typedef map<string, Equate> LabelToAddr;

  private:
    // Extract labels and values from the given character stream
    string extractLabel(char *c) const;
    int extractValue(char *c) const;

    // Count completions for the given mapping
    int countCompletions(const char *in, LabelToAddr& addresses);

  private:
    CartState myState;
    CartState myOldState;

    DisassemblyList myDisassembly;

    LabelToAddr mySystemAddresses;
    AddrToLabel mySystemReadLabels;   // labels used in a read context
    AddrToLabel mySystemWriteLabels;  // labels used in a write context

    LabelToAddr myUserAddresses;
    AddrToLabel myUserLabels;

    RamAreaList myRamAreas;

    string myCompletions;
    string myCompPrefix;

    uInt16 myRWPortAddress;

    enum { kSystemEquateSize = 158 };
    static const Equate ourSystemEquates[kSystemEquateSize];

//////////////////////////////////////////////
    /**
      Enumeration of the 6502 addressing modes
    */
    enum AddressingMode
    {
      Absolute, AbsoluteX, AbsoluteY, Immediate, Implied,
      Indirect, IndirectX, IndirectY, Invalid, Relative,
      Zero, ZeroX, ZeroY
    };

    /**
      Enumeration of the 6502 access modes
    */
    enum AccessMode
    {
      Read, Write, None
    };

    /// Addressing mode for each of the 256 opcodes
    /// This specifies how the opcode argument is addressed
    static AddressingMode AddressModeTable[256];

    /// Access mode for each of the 256 opcodes
    /// This specifies how the opcode will access its argument
    static AccessMode AccessModeTable[256];

    /// Table of instruction mnemonics
    static const char* InstructionMnemonicTable[256];
//////////////////////////////////////////////
};

#endif