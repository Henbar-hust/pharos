// Copyright 2015, 2016 Carnegie Mellon University.  See LICENSE file for terms.

#include "globals.hpp"
#include "masm.hpp"
#include "descriptors.hpp"
#include "datatypes.hpp"
#include "util.hpp"
#include "types.hpp"
#include "typedb.hpp"

namespace pharos {

GlobalMemoryDescriptor::GlobalMemoryDescriptor() {
  address = 0;
  initialized = false;
  in_image = false;
  confidence = ConfidenceNone;
  type = DTypeNone;
  size = 0;
  access_size = 0;

  // Because we have no concept of NULL symbolic values, empty will have to do

  values.push_back(SymbolicValuePtr());
  memory_address = SymbolicValuePtr();
}

GlobalMemoryDescriptor::GlobalMemoryDescriptor(rose_addr_t addr) {
  address = addr;
  confidence = ConfidenceNone;
  type = DTypeNone;
  size = 0;
  access_size = 0;
  analyze(addr);

  // defaulting to the width of the architcture. This is not really right, but not really
  // wrong either
  size_t arch_bits = global_descriptor_set->get_arch_bits();
  memory_address = SymbolicValue::constant_instance(arch_bits, addr);
  values.push_back(SymbolicValue::variable_instance(arch_bits));
}


std::string
GlobalMemoryDescriptor::to_string() const {
  std::stringstream ostr;

  ostr << "Global variable at " << address_string();
  if (memory_address) {
    TreeNodePtr global_addr_tnp = memory_address->get_expression();
    if (global_addr_tnp) {

      // TODO: This is useful for this when done debugging
      int64_t addr_raw = reinterpret_cast<int64_t>(&*global_addr_tnp);
      ostr << ", " << "raw=(" << addr_str(addr_raw) << ")";

      TypeDescriptorPtr addr_td = fetch_type_descriptor(global_addr_tnp);
      ostr << " addr=" << *global_addr_tnp << " addr td=(" << addr_td->to_string() << ")";
    }
    else {
      ostr << " address=(invalid)";
    }
  }

  auto global_values = values;
  if (global_values.empty() == false) {

    size_t i = 0;
    ostr << " values=({";

    for (auto gv : global_values) {
      TreeNodePtr val_tnp = gv->get_expression();
      if (val_tnp) {
        ostr << "(exp=(" << *val_tnp << ")";
      }
      else {
        ostr << "<unknown>";
      }
      TypeDescriptorPtr global_value_tdp = fetch_type_descriptor(gv);
      ostr << " td=";
      if (global_value_tdp) {
        ostr << "(" << global_value_tdp->to_string() << ")";
      }
      else {
        ostr << "<unknown>";
      }
      ostr << ")";

      if (i+1 < global_values.size())  ostr << ", ";

      ++i;
    }
    ostr << "})";
  }
  else {
    std::cout << " values=(invalid)";
  }
  return ostr.str();
}

void GlobalMemoryDescriptor::add_value(SymbolicValuePtr new_val) {
  values.push_back(new_val);
}

void GlobalMemoryDescriptor::short_print(std::ostream &o) const {
  o << "Global: addr=" << address_string();

  o << " refs=[";
  for (const SgAsmInstruction* insn : refs) {
    o << boost::str(boost::format(" 0x%08X") % insn->get_address());
  }
  o << " ]";

  o << " reads=[";
  for (const SgAsmInstruction* insn : reads) {
    o << boost::str(boost::format(" 0x%08X") % insn->get_address());
  }
  o << " ]";

  o << " writes=[";
  for (const SgAsmInstruction* insn : writes) {
    o << boost::str(boost::format(" 0x%08X") % insn->get_address());
  }
  o << " ]";
}

void GlobalMemoryDescriptor::print(std::ostream &o) const {
  o << "Global: addr=" << address_string()
    << " image=" << in_image << " init=" << initialized
    << " asize=" << access_size << " tsize=" << size << LEND;

  for (const SgAsmInstruction* insn : refs) {
    o << "   Ref: " << debug_instruction(insn) << LEND;
  }
  for (const SgAsmInstruction* insn : reads) {
    o << "  Read: " << debug_instruction(insn) << LEND;
  }
  for (const SgAsmInstruction* insn : writes) {
    o << " Write: " << debug_instruction(insn) << LEND;
  }
}

const InsnSet& GlobalMemoryDescriptor::get_writes() { return writes; }
const InsnSet& GlobalMemoryDescriptor::get_reads() { return reads; }
const InsnSet& GlobalMemoryDescriptor::get_refs() { return refs; }

// Analyze a memory address (most commonly during construction).
void GlobalMemoryDescriptor::analyze(rose_addr_t addr) {
  in_image = global_descriptor_set->memory_in_image(addr);
  //GINFO << "Memory at " << address_string() << " in_image=" << in_image << LEND;

  // Cory says: Robb changed our ability to detect whether an address was initialized or not,
  // and since we weren't really using it for anything anayway, I disabled it.
  initialized = false;
  // initialized = global_descriptor_set->memory_initialized(addr);
  //GINFO << "Memory at " << address_string() << " initialized=" << initialized << LEND;
}

// Are all known memory accesses reads?
bool GlobalMemoryDescriptor::read_only() const {
  if (writes.size() == 0 && reads.size() > 0) return true;
  return false;
}

// Are there both read and write memory accesses?
bool GlobalMemoryDescriptor::read_write() const {
  if (writes.size() > 0 && reads.size() > 0) return true;
  return false;
}

// Is the descriptor known to be used in memory accesses?
bool GlobalMemoryDescriptor::known_memory() const {
  if (writes.size() > 0 || reads.size() > 0) return true;
  return false;
}

// Is the descriptor "suspicious"?  (One of several unlikely cases?)
bool GlobalMemoryDescriptor::suspicious() const {
  // If we've got no reads, we're probably just missing them.
  if (reads.size() == 0) return true;
  return false;
}

void GlobalMemoryDescriptor::add_read(SgAsmInstruction* insn, int asize) {
  // Add the instruction to the reads list.
  reads.insert(insn);

  // Remove the instruction from refs now that we know that it's really a read.
  InsnSet::iterator it = refs.find(insn);
  if (it != refs.end()) refs.erase(it);

  // Update the access size appropriately.
  if (access_size == 0) access_size = asize;
  if (access_size != asize) access_size = -1;
}

void GlobalMemoryDescriptor::add_write(SgAsmInstruction* insn, int asize) {
  // Add the instruction to the writes list.
  writes.insert(insn);

  // Remove the instruction from refs now that we know that it's really a read.
  InsnSet::iterator it = refs.find(insn);
  if (it != refs.end()) refs.erase(it);

  // Update the access size appropriately.
  if (access_size == 0) access_size = asize;
  if (access_size != asize) access_size = -1;
}

// This is here primarily because I don't want to reference the global descriptor set in the
// globals header file.
void TypeBase::read(void *b) {
  // This is a BUG!   We need to thow an exception here, for now just WARN.
  if (address == 0 || size == 0) {
    GWARN << "Bad read of address and size. address=0x" << std::hex
          << address << " size=0x" << size << std::dec << LEND;
    return;
  }
  global_descriptor_set->read_mem(address, (char *)b, size);
}

void TypeBase::read(rose_addr_t a, void *b, size_t s) {
  global_descriptor_set->read_mem(a, (char *)b, s);
}

void TypeString::read(rose_addr_t a) {
  size = 0;
  value.clear();
  TypeByte ch(a);
  while (ch.read(a + size) != 0) {
    size += 1;
    value += ch.value;
  }
  // The NULL is counted in the size.
  size += 1;
}

TypeUnicodeString::TypeUnicodeString(rose_addr_t a): TypeBase(a, 0) {
  size = 0;
  value.clear();
  TypeWideChar ch(a);
  while (ch.read(a + size) != 0) {
    size += 2;
    value += ch.value;
  }
  // The NULL is counted in the size.
  size += 1;
}

TypeLen8String::TypeLen8String(rose_addr_t a): TypeString(a) {
  TypeByte len(a);
  char buffer[257];
  TypeBase::read(a + 1, buffer, len.value);
  size = len.value + 1;
  value = buffer;
}

TypeLen16String::TypeLen16String(rose_addr_t a): TypeString(a) {
  TypeWord len(a);
  char* buffer = (char *)alloca(len.value);
  TypeBase::read(a + 2, buffer, len.value);
  size = len.value + 2;
  value = buffer;
}

TypeLen32String::TypeLen32String(rose_addr_t a): TypeString(a) {
  TypeDword len(a);
  if (len.value <= 0x1000000) {
    char* buffer = (char *)alloca(len.value);
    TypeBase::read(a + 2, buffer, len.value);
    size = len.value + 4;
    value = buffer;
  }
  else {
    GERROR << "Over-sized string ignored." << LEND;
    // We should probably be throwing an error here as well.
  }
}

// This structure may not get used much, because it's typically on the stack, and is
// constructed from several push instructions in the code.
// This is defined in the Microsoft source code as: _EH3_EXCEPTION_REGISTRATION
void TypeSEH3ExceptionRegistration::read(rose_addr_t a) {
  address = a;
  size = 16;
  Next.read(a);
  ExceptionHandler.read(a + 4);
  ScopeTable.read(a + 8);
  TryLevel.read(a + 12);
}

std::string TypeSEH3ExceptionRegistration::str() const {
  return boost::str(boost::format("<next=%s ehfunc=%s scopetable=%s lvl=%s>") %
             Next.str() % ExceptionHandler.str() %
             ScopeTable.str() % TryLevel.str());
}

// This is defined in the Microsoft source code as:
void TypeSEH4ScopeTableRecord::read(rose_addr_t a) {
  address = a;
  size = 12;
  EnclosingLevel.read(a);
  FilterFunc.read(a + 4);
  HandleFunc.read(a + 8);
}

std::string TypeSEH4ScopeTableRecord::str() const {
  return boost::str(boost::format("<lvl=%s filter=%s handler=%s>") %
             EnclosingLevel.str() % FilterFunc.str() % HandleFunc.str());
}

// This is defined in the Microsoft source code as:
void TypeSEH4ScopeTable::read(rose_addr_t a) {
  address = a;
  size = 16;
  GSCookieOffset.read(a);
  GSCookieXOROffset.read(a + 4);
  EHCookieOffset.read(a + 8);
  EHCookieXOROffset.read(a + 12);

  TypeSEH4ScopeTableRecord scope;
  do {
    scope.read(a + size);
    ScopeRecord.push_back(scope);
    size += scope.size;
  } while ((int32_t)scope.EnclosingLevel.value != -2);
  // Negative two appears to be the magic value for EH4.
  // Negative one is reported to be the approprioate value for EH3.
}

std::string TypeSEH4ScopeTable::str() const {
  std::string base = boost::str(boost::format("<gsc=%s gscx=%s ehc=%s ehcx=%s>") %
                         GSCookieOffset.str() %
                         GSCookieXOROffset.str() %
                         EHCookieOffset.str() %
                         EHCookieXOROffset.str());
  base += "recs=[ ";
  for (const TypeSEH4ScopeTableRecord & scope : ScopeRecord) {
    base += scope.str();
  }
  base += " ]";
  return base;
}

void TypeSEH4TryBlockMapEntry::read(rose_addr_t a) {
  address = a;
  size = 20;
  tryLow.read(a);
  tryHigh.read(a + 4);
  catchHigh.read(a + 8);
  nCatches.read(a + 12);
  pHandlerArray.read(a + 16);

  //rose_addr_t handler_pointer = pHandlerArray.value;
  //for (int i = 0; i < maxState.value; i++) {
  //  TypeSEH4UnwindMapEntry entry(unwind_map_pointer);
  //  unwind_map.push_back(entry);
  //  GINFO << entry.str() << LEND;
  //  unwind_map_pointer += entry.size;
  //}
}

std::string TypeSEH4TryBlockMapEntry::str() const {
  return boost::str(boost::format("<trylow=%s tryhigh=%s catchhigh=%s ncatches=%s handlers=%s>") %
             tryLow.str() % tryHigh.str() % catchHigh.str() %
             nCatches.str() % pHandlerArray.str());
}

void TypeSEH4HandlerType::read(rose_addr_t a) {
  adjectives.read(a);
  pType.read(a + 4);
  dispatchObj.read(a + 8);
  addressOfHandler.read(a + 12);
}

std::string TypeSEH4HandlerType::str() const {
  return boost::str(boost::format("<adj=%s type=%s obj=%s handler=%s>") %
             adjectives.str() % pType.str() %
             dispatchObj.str() % addressOfHandler.str());
}

void TypeSEH4UnwindMapEntry::read(rose_addr_t a) {
  address = a;
  size = 8;
  toState.read(a);
  action.read(a + 4);
}

std::string TypeSEH4UnwindMapEntry::str() const {
  return boost::str(boost::format("<tostate=%s action=%s>") % toState.str() % action.str());
}

// _s_FuncInfo
void TypeSEH4FuncInfo::read(rose_addr_t a) {
  address = a;
  size = 34;
  magicNumber.read(a);
  if (magicNumber.value < 0x19930520 || magicNumber.value > 0x19930522) {
    GERROR << "Invalid magic number for TypeSEH4FuncInfo magic=" << magicNumber.str() << LEND;
    // We should also be throwing an exception here...
    return;
  }

  maxState.read(a + 4);
  pUnwindMap.read(a + 8);
  rose_addr_t unwind_map_pointer = pUnwindMap.value;
  for (unsigned int i = 0; i < maxState.value; i++) {
    TypeSEH4UnwindMapEntry entry(unwind_map_pointer);
    unwind_map.push_back(entry);
    unwind_map_pointer += entry.size;
  }

  nTryBlocks.read(a + 12);
  pTryBlocksMap.read(a + 16);

  rose_addr_t try_block_map_pointer = pTryBlocksMap.value;
  for (unsigned int i = 0; i < nTryBlocks.value; i++) {
    TypeSEH4TryBlockMapEntry entry(try_block_map_pointer);
    try_block_map.push_back(entry);
    try_block_map_pointer += entry.size;
  }
  // Add contiguos block to memory map

  // These two fields are reportedly only used in x64.
  nIPMapEntries.read(a + 20);
  pIPtoStateMap.read(a + 24);
  if (magicNumber.value > 0x19930520) {
    pESTypeList.read(a + 28);
  }
  if (magicNumber.value > 0x19930521) {
    EHFlags.read(a + 32);
  }
}

std::string TypeSEH4FuncInfo::str() const {
  return boost::str(boost::format(
    "<magic=%s states=%s unwind=%s ntries=%s trymap=%s nip=%s ipm=%s estypes=%s flags=%s>") %
             magicNumber.str() %
             maxState.str() % pUnwindMap.str() %
             nTryBlocks.str() % pTryBlocksMap.str() %
             nIPMapEntries.str() % pIPtoStateMap.str() %
             pESTypeList.str() % EHFlags.str());
}

void TypeSEH4FuncInfo::dump() {
  GINFO << "SEH4FuncInfo @0x" << std::hex << address << std::dec << ": " << str() << LEND;
  for (const TypeSEH4UnwindMapEntry & entry : unwind_map) {
    GINFO << "  " << entry.str() << LEND;
  }
  for (const TypeSEH4TryBlockMapEntry & entry : try_block_map) {
    GINFO << "  " << entry.str() << LEND;
  }
}

void TypeRTCVarDesc::read(rose_addr_t a) {
  address = a;
  size = 12;
  var_offset.read(a);
  var_size.read(a + 4);
  var_name_addr.read(a + 8);

  var_name.read(var_name_addr.value);
}

std::string TypeRTCVarDesc::str() const {
  return boost::str(boost::format("<offset=%s size=%s name='%s'>") %
                    var_offset.str() % var_size.str() % var_name.str());
}

void TypeRTCFrameDesc::read(rose_addr_t a) {
  address = a;
  size = 8;
  varCount.read(a);
  variables.read(a + 4);

  rose_addr_t var_desc = variables.value;
  for (unsigned int i = 0; i < varCount.value; i++) {
    TypeRTCVarDesc var(var_desc);
    vars.push_back(var);
    var_desc += var.size;
  }
}

std::string TypeRTCFrameDesc::str() const {
  return boost::str(boost::format("<count=%s>") % varCount.str());
}

void TypeRTCFrameDesc::dump() {
  GINFO << "RTCFrameDesc @0x" << std::hex << address << std::dec << ": " << str() << LEND;
  for (const TypeRTCVarDesc & var : vars) {
    GINFO << "  " << var.str() << LEND;
  }
}

void TypeRTTITypeDescriptor::read(rose_addr_t a) {
  address = a;
  pVFTable.read(a);
  spare.read(a + 4);
  name.read(a + 8);
  size = 8 + name.size;
}

std::string TypeRTTITypeDescriptor::str() const {
  return boost::str(boost::format("<vftable=%s name='%s'>") % pVFTable.str() % name.str());
}

void TypeRTTICompleteObjectLocator::read(rose_addr_t a) {
  address = a;
  size = 20;
  signature.read(a);
  offset.read(a + 4);
  cdOffset.read(a + 8);
  pTypeDescriptor.read(a + 12);
  pClassDescriptor.read(a + 16);

  type_desc.read(pTypeDescriptor.value);
  class_desc.read(pClassDescriptor.value);
}

std::string TypeRTTICompleteObjectLocator::str() const {
  return boost::str(boost::format("<sig=%s offset=%s cdo=%s type=%s class=%s>")
                    % signature.str() % offset.str() % cdOffset.str() %
                    pTypeDescriptor.str() % pClassDescriptor.str());
}

void TypeRTTICompleteObjectLocator::dump() {
  GINFO << "RTTI Object @0x" << std::hex << address << std::dec << ": " << str() << LEND;
  GINFO << "  Type: " << type_desc.str() << LEND;
  GINFO << "  Class: " << class_desc.str() << LEND;
  for (const TypeRTTIBaseClassDescriptor & bcd : class_desc.base_classes) {
    GINFO << "    Base Class: " << bcd.str() << LEND;
  }
}

void TypeRTTIClassHierarchyDescriptor::read(rose_addr_t a) {
  address = a;
  size = 16;
  signature.read(a);
  attributes.read(a + 4);
  numBaseClasses.read(a + 8);
  pBaseClassArray.read(a + 12);

  rose_addr_t base_class_array = pBaseClassArray.value;
  for (unsigned int i = 0; i < numBaseClasses.value; i++) {
    TypeDwordAddr bcaddr(base_class_array);
    TypeRTTIBaseClassDescriptor bcd(bcaddr.value);
    base_classes.push_back(bcd);
    base_class_array += bcaddr.size;
  }
}

std::string TypeRTTIClassHierarchyDescriptor::str() const {
  return boost::str(boost::format("<sig=%s attr=%s numbases=%s>") %
                    signature.str() % attributes.str() % numBaseClasses.str());
}

void TypeRTTIBaseClassArray::read(UNUSED rose_addr_t a) {
}

std::string TypeRTTIBaseClassArray::str() const {
  return boost::str(boost::format("<incomplete>"));
}

void TypeRTTIBaseClassDescriptor::read(rose_addr_t a) {
  address = a;
  size = 28;
  pTypeDescriptor.read(a);
  numContainedBases.read(a + 4);
  where_mdisp.read(a + 8);
  where_pdisp.read(a + 12);
  where_vdisp.read(a + 16);
  attributes.read(a + 20);
  pClassDescriptor.read(a + 24);

  type_desc.read(pTypeDescriptor.value);

  // This quickly turns into a recursive relationship, and could even be a cycle, so we'll need
  // to give some more thought to whether we ought to be doing this.  A visited list is
  // probably required, and we definitely don't want to store the whole tree inside each object
  // as we would do if this were a member variable instead of a discarded local.  I was trying
  // to read it here because I wanted it to throw if there's a problem during reading, but it
  // turns out that even normal files have endless loops.

  //TypeRTTIClassHierarchyDescriptor chd;
  //chd.read(pClassDescriptor.value);
}

std::string TypeRTTIBaseClassDescriptor::str() const {
  return boost::str(boost::format("<type=%s numbase=%s pmd=(%s,%s,%s) attr=%s>") %
                    pTypeDescriptor.str() % numContainedBases.str() %
                    where_mdisp.str() % where_pdisp.str() % where_vdisp.str() %
                    attributes.str());
}

} // namespace pharos

/* Local Variables:   */
/* mode: c++          */
/* fill-column:    95 */
/* comment-column: 0  */
/* End:               */
