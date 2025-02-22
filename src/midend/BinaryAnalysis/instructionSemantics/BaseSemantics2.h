#ifndef ROSE_BinaryAnalysis_InstructionSemantics2_BaseSemantics_H 
#define ROSE_BinaryAnalysis_InstructionSemantics2_BaseSemantics_H

#include "BinarySmtSolver.h"
#include "Diagnostics.h"
#include "FormatRestorer.h"
#include "Registers.h"
#include "RoseException.h"

#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/optional.hpp>
#include <boost/serialization/access.hpp>
#ifndef USE_ROSE                                        // [Robb P Matzke 2016-11-11]: ROSE cannot compile this header
#include <boost/serialization/shared_ptr.hpp>
#endif
#include <boost/serialization/string.hpp>
#include <Sawyer/Assert.h>
#include <Sawyer/IntervalMap.h>
#include <Sawyer/IntervalSetMap.h>
#include <Sawyer/Map.h>
#include <Sawyer/Optional.h>
#include <Sawyer/Set.h>

namespace Rose {
namespace BinaryAnalysis {

/** Binary instruction semantics.
 *
 *  Entities in this namespace deal with the semantics of machine instructions, and with the process of "executing" a machine
 *  instruction in a particular semantic domain.  Instruction "execution" is a very broad term and can refer to execution in
 *  the tranditional sense where each instruction modifies the machine state (registers and memory) in a particular domain
 *  (concrete, interval, sign, symbolic, user-defined).  But it can also refer to any kind of analysis that depends on
 *  semantics of individual machine instructions (def-use, tainted-flow, etc).  It can even refer to the transformation of
 *  machine instructions in ROSE internal representation to some other representation (e.g., to ROSE RISC or LLVM assembly)
 *  where the other representation is built by "executing" the instruction.
 *
 * @section instruction_semantics_components Components of instruction semantics
 *
 *  ROSE's binary semantics framework has four major components: the dispatchers, RISC operators, states, and values.  Each
 *  component has a base class to define the interface and common functionality, and subclasses to provide implementation
 *  details.  A semantics framework is constructed at runtime by instantiating objects from these subclasses and connecting the
 *  objects together to form a lattice.
 *
 *  At the top of the lattice is a <em>dispatcher</em> (base class @ref BaseSemantics::Dispatcher "Dispatcher") that "executes"
 *  machine instructions by translating (or <em>lowering</em>) them to sequences of RISC-like operations. The subclasses of
 *  @ref BaseSemantics::Dispatcher "Dispatcher" implement various instruction set architectures (ISAs).
 *
 *  The dispatcher points to an object that defines the RISC-like operators. This object is instantiated from a subclass of
 *  @ref BaseSemantics::RiscOperators "RiscOperators", and defines the few dozen RISC-like operators in terms of modifications
 *  to a state, or collection of values.  Therefore, the @ref BaseSemantics::RiscOperators "RiscOperators" needs to point to
 *  the <em>current state</em>. Depending on the subclass, it might also point to a lazily-initialized <em>initial
 *  state</em>. It is common for an analysis to swap new states in and out of the @ref BaseSemantics::RiscOperators
 *  "RiscOperators" while the analysis runs.
 *
 *  The aforementioned states are objects instantiated from subclasses of @ref BaseSemantics::State "State", which points to at
 *  least two substate objects: a @ref BaseSemantics::MemoryState "MemoryState" that describes the values stored at memory
 *  addresses, and a @ref BaseSemantics::RegisterState "RegisterState" that describes the values stored in registers. Depending
 *  on the @ref BaseSemantics::State "State" subclass, a state may also contain additional data. The @ref
 *  BaseSemantics::MemoryState "MemoryState" and @ref BaseSemantics::RegisterState "RegisterState" are base classes, and their
 *  subclasses provide various mechanisms for storing the memory and registers. For instance, memory might be stored as a
 *  chronological list or a map, and registers might be stored as an array or map.
 *
 *  Up to this point, we haven't nailed down the definition of "value".  A semantic value is also an abstract concept whose
 *  interface is declared in the @ref BaseSemantics::SValue "SValue" base class, the subclasses of which define the details. A
 *  value could be a vector of bits (concrete); an interval defined by two concrete endpoints; a sign consisting of one of the
 *  values positive, negative, zero, top, or bottom; a symbolic expression composed of constants, variables, and operations; or
 *  pretty much anything you want as long as it implements the API defined in the @ref BaseSemantics::SValue "SValue" base
 *  class.  Many of the objects mentioned above need to be able to create new values, and therefore they point to a
 *  proto-typical value instance which forms the bottom of the lattice.
 *  
 *  Not all combinations of dispatcher, operators, states, and values are possible, although they are intended to be mostly
 *  interchangeable.  For instance, you could combine an x86 dispatcher with symbolic operators using chronological memory and
 *  generic register states and a symbolic value type, but it probably doesn't make sense to have all the same components but
 *  replacing the symbolic value type with a concrete value type.  To help keep things organized, collections of compatible
 *  types are placed in namespaces such as @ref SymbolicSemantics. These collections of compatible semantic types are called
 *  <em>semantic domains</em>. Mixing types between semantic domains sometimes works, depending on the domain.
 *
 * @section instruction_semantics_domains Major domains
 *
 *  As mentioned, a "semantic domain" is a collection of compatible semantic types contained in a namespace. ROSE provides a
 *  number of general-purpose domains but users are also expected to specialize these for specific purposes. Even within ROSE,
 *  many of the analyses specialize these general-purpose domains in order to do something more specific without needing to
 *  re-implement large portions of the infrastructure.
 *
 *  You can find the full list of general-purpose domains by looking for sub namespaces of this namespace whose
 *  names end with the word "Semantics".  Some important examples:
 *
 *  @li @ref ConcreteSemantics has values that are bit vectors. This domain would be a good base class if you were writing an
 *  emulator since emulators work only with specific known values (i.e., concrete values rather than intervals, symbolic
 *  expressions, etc.). An example of a concrete value is a 16-bit vector 1010101111001110.
 *
 *  @li @ref IntervalSemantics is a domain whose values are sets of non-contiguous ranges of values where each range is
 *  indicated by its first and last concrete value.  An example of a value in this domain is the set {[0000,0010], [1000,1011]}
 *  that represents the four-bit concrete values 0000, 0001, 0010, 1000, 1001, 1010, and 1011.
 *
 *  @li @ref SymbolicSemantics is a domain whose values are constants, variables, and operations that form an expression
 *  tree. Each vertex of the tree has a specific width measured in bits. Variable are named with the letter "v" followed by a
 *  decimal serial number as in "v1", "v2", etc.  The symbolic @ref SymbolicSemantics::SValue "SValue" points to the symbolic
 *  expression. Symbolic expression trees (which are immutable) are instances of @ref Rose::BinaryAnalysis::SymbolicExpr.
 *
 *  @section instruction_semantics_pointers Memory Management
 *
 *  Most of the instruction semantics objects are allocated on the heap and are reference counted. This is beneficial to the
 *  user because an analysis might create millions of objects and it would otherwise be a burden if the analysis author had to
 *  know when it was safe to delete objects.  It also allows an analysis to return results to a higher level and not worry
 *  about who now owns those objects.
 *
 *  There are two ways to allocate such objects: (1) you must know the name of the derived class from which to instantiate an
 *  object, or (2) you must have an instance of an object of the class you wish to instantiate.  The former method is used when
 *  you're constructing a semantics framework since that's the moment you know the names of the classes, and the latter method
 *  is used when the framework is running and the class names might not be known but an object is already available. The former
 *  method uses static member functions, and the latter method uses virtual member functions (C++ implementation of the OO term
 *  <em>virtual constructor</em>).
 *
 *  Additional information can be found under @ref instruction_semantics_specialization.
 *
 *  @section instruction_semantics_create Instantiating a Semantics Framework
 *
 *  Let's say you have an analysis that needs to process x86 instructions symbolically. The first thing you need to do is
 *  instantiate a semantics framework -- the lattice of objects that are instantiated from the particular semantics component
 *  subclasses.  You'll need a @ref BaseSemantics::DispatcherX86 "DispatcherX86" object to handle the instructions, which
 *  invokes the RISC-like operations defined by an instance of @ref SymbolicSemantics::RiscOperators, which can use a
 *  chronological memory state and a generic register state, and whose values are symbolic (@ref
 *  SymbolicSemantics::SValue). You'll need to tie all these objects together into a lattice with the disptatcher at the top.
 *  The constructors for the various components generally take arguments which are the lower layers of the lattice, therefore
 *  you'll need to build the lattice from the bottom up; that is, start by constructing a proto-typical value (i.e., a value
 *  from which new values can be created), then the register and memory states, which are then joined together into a single
 *  state object. Then create the RISC-like operations object and give it an initial state, and finally create the dispatcher
 *  that points to the RISC-like operations.
 *
 *  Most semantic domains (@ref SymbolicSemantics included) have a simplified @ref BaseSemantics::RiscOperators "RiscOperators"
 *  constructor that uses default types for some of the lower components, but we'll show the full monty here:
 *
 *  @code
 *      namespace Base = Rose::BinaryAnalysis::InstructionSemantics2::BaseSemantics;
 *      namespace Symbolic = Rose::BinaryAnalysis::InstructionSemantics2::SymbolicSemantics;
 *      namespace P2 = Rose::BinaryAnalysis::Partitioner2;
 *
 *      P2::Partitioner partitioner = ....; // disassembly results
 *      SmtSolverPtr solver = ....; // optional SMT solver
 *
 *      const RegisterDictionary *regdict = partitioner.instructionProvider().registerDictionary();
 *      Base::SValuePtr prototval = Symbolic::SValue::instance();
 *      Base::RegisterStatePtr regs = Symbolic::RegisterState::instance(protoval, regdict);
 *      Base::MemoryStatePtr mem = Symbolic::MemoryListState::instance(protoval, protoval);
 *      Base::StatePtr state = Symbolic::State::instance(regs, mem);
 *      Base::RiscOperatorsPtr ops = Symbolic::RiscOperators::instance(state, solver);
 *      Base::DispatcherPtr cpu = Base::DispatcherX86::instance(ops, 32, regdict);
 *  @endcode
 *
 *  @section instruction_semantics_specialization Specialization
 *
 *  The instruction semantics architecture is designed to allow users to specialize nearly every part of it, which is useful
 *  when creating an analysis that needs to override some small parts of the entire semantics framework. Lets say you need to
 *  write an analysis that uses a concrete domain (like a simulator) and you want it to report every memory address to which a
 *  value is written.  Such a domain would be identical in every respect to the ROSE-provided @ref ConcreteSemantics domain
 *  except the RISC-like operation for writing a value to memory needs to additionally print the address and value.
 *
 *  Since you're essentially creating a new domain derived from ROSE's concrete domain, you should create a namespace for your
 *  domain. Let's call it @c MySemantics.  Since the value type, memory state, register state, combined state are all the same,
 *  create typedefs within @c MySemantics that just alias the types in ROSE's concrete domain. Notice there's no alias for a
 *  dispatcher; this is because dispatchers are domain-agnostic--any dispatcher will work with any semantic domain.
 *
 *  The only class you need to change is the RiscOperators class in ROSE's concrete domain. Therefore, within your namespace,
 *  define a new class named @c MySemantics::RiscOperators that inherits from ROSE's @ref ConcreteSemantics::RiscOperators, and
 *  override the @c writeMemory method so it prints the address and value before delegating to the base class.  You'll also
 *  need to define three classes of constructors detailed in @ref instruction_semantics_constructors.
 *
 *  Finally, your analysis can instantiate a semantics framework using the components from the new @c MySemantics. The code to
 *  do this looks almost identical to the example instantiation we already saw, except the word @c Symbolic would be changed to
 *  @c MySemantics wherever it appears.
 *
 *  @section instruction_semantics_constructors Writing Constructors for Reference-counted Classes
 *
 *  Here are some additional details to help you implement subclasses of reference-counted classes: You should implement three
 *  versions of each constructor: the real C++ constructor, the static allocating constructor, and the virtual constructor.
 *  Fortunately, the amount of extra code needed is not substantial since the virtual constructor can call the static
 *  allocating constructor, which can call the real C++ constructor. You'll need to override each overload of the three
 *  versions of constructors from the base class. The three versions in more detail are:
 *
 *  @li <i>Real Constructors</i>: These are the normal C++ constructors. They should have protected access and are used
 *  only by authors of subclasses.  The other two versions of constructors described next are <em>constructors</em> in the
 *  sense of object-oriented languages in general, although in the C++ world they're not called "constructors".
 *
 *  @li <i>Static Allocating Constructors</i>: These are class methods (C++ static member functions) that allocate a specific
 *  kind of object on the heap and return a smart pointer to the object.  They are named "instance" to emphasize that they
 *  instantiate a new instance of a particular class and they return the pointer type that is specific to the class (i.e., not
 *  one of the BaseSemantics pointer types).  When an end user constructs a dispatcher, RISC operators, etc., they have
 *  particular classes in mind and use those classes' "instance" methods to create objects.  Static allocating constructors are
 *  seldom called by authors of subclasses; instead the author usually has an object whose provenance can be traced back to a
 *  user-created object (such as a prototypical object), and he invokes one of that object's virtual constructors.
 *
 *  @li <i>Virtual Constructors</i>: A virtual constructor creates a new object having the same run-time type as the object on
 *  which the method is invoked.  Virtual constructors are often named "create" with the virtual copy constructor named
 *  "copy" or "clone", however the SValue class hierarchy follows a different naming scheme for historic reason--its virtual
 *  constructors end with an underscore.  Virtual constructors return pointer types that defined in BaseSemantics. Subclass
 *  authors usually use this kind of constructor because it frees them from having to know a specific type and allows
 *  their classes to be easily subclassed. Virtual constructors are implemented in C++ as virtual member functions.
 *
 *  When writing a subclass the author should implement the three versions for each constructor inherited from the super
 *  class. The author may also add any additional constructors that are deemed necessary, realizing that all subclasses of his
 *  class will also need to implement those constructors.
 *
 *  The subclass may define a public virtual destructor that will be called by the smart pointer implementation when the final
 *  pointer to the object is destroyed.
 *
 *  Here is an example of specializing a class that is itself derived from something in ROSE semantics framework.
 *
 *  @code
 *      // Smart pointer for the subclass
 *      typedef boost::shared_ptr<class MyThing> MyThingPtr;
 *
 *      // Class derived from OtherThing, which eventually derives from a class
 *      // defined in BinarySemantics::InstructionSemantics2::BaseSemantics--lets
 *      // say BaseSemantics::Thing -- a non-existent class that follows the rules
 *      // outlined above.
 *      class MyThing: public OtherThing {
 *      private:
 *          char *data; // some data allocated on the heap w/out a smart pointer
 *
 *          // Real constructors.  Normally this will be all the same constructors as
 *          // in the super class, and possibly a few new ones.  Thus anything you add
 *          // here will need to also be implemented in all subclasses hereof. Lets
 *          // pretend that the super class has two constructors: a copy constructor
 *          // and one that takes a pointer to a register state.
 *      protected:
 *          explicit MyThing(const BaseSemantics::RegisterStatePtr &rstate)
 *              : OtherThing(rstate), data(NULL) {}
 *
 *          MyThing(const MyThing &other)
 *              : OtherThing(other), data(copy_string(other.data)) {}
 *
 *          // Define the virtual destructor if necessary.  This won't be called until
 *          // the last smart pointer reference to this object is destroyed.
 *      public:
 *          virtual ~MyThing() {
 *              delete data;
 *          }
 *
 *          // Static allocating constructors. One static allocating constructor
 *          // for each real constructor, including the copy constructor.
 *      public:
 *          static MyThingPtr instance(const BaseSemantics::RegisterStatePtr &rstate) {
 *              return MyThingPtr(new MyThing(rstate));
 *          }
 *
 *          static MyThingPtr instance(const MyThingPtr &other) {
 *              return MyThingPtr(new MyThing(*other));
 *          }
 *
 *          // Virtual constructors. One virtual constructor for each static allocating
 *          // constructor.  It is of utmost importance that we cover all the virtual
 *          // constructors from the super class. These return the most super type
 *          // possible, usually something from BaseSemantics.
 *      public:
 *          virtual BaseSemantics::ThingPtr create(const BaseSemantics::RegisterStatePtr &rstate) {
 *              return instance(rstate);
 *          }
 *
 *          // Name the virtual copy constructor "clone" rather than "create".
 *          virtual BaseSemantics::ThingPtr clone(const BaseSemantics::ThingPtr &other_) {
 *              MyThingPtr other = MyThing::promote(other_);
 *              return instance(other);
 *          }
 *
 *          // Define the checking dynamic pointer cast.
 *      public:
 *          static MyThingPtr promomte(const BaseSemantics::ThingPtr &obj) {
 *              MyThingPtr retval = boost::dynamic_pointer_cast<MyThingPtr>(obj);
 *              assert(retval!=NULL);
 *              return NULL;
 *          }
 *
 *          // Define the methods you need for this class.
 *      public:
 *          virtual char *get_data() const {
 *              return data; // or maybe return a copy in case this gets deleted?
 *          }
 *          virtual void set_data(const char *s) {
 *              data = copy_string(s);
 *          }
 *      private:
 *          void char *copy_string(const char *s) {
 *              if (s==NULL)
 *                  return NULL;
 *              char *retval = new char[strlen(s)+1];
 *              strcpy(retval, s);
 *              return retval;
 *          }
 *     };
 *  @endcode
 *
 */
namespace InstructionSemantics2 {

/** Diagnostics logging facility for instruction semantics. */
extern Sawyer::Message::Facility mlog;

/** Initialize diagnostics for instruction semantics. */
void initDiagnostics();

/** Base classes for instruction semantics.  Basically, anything that is common to two or more instruction semantic
 *  domains will be factored out and placed in this name space. */
namespace BaseSemantics {

class RiscOperators;

/** Format for printing things. Some semantic domains may want to pass some additional information to print methods on a
 *  per-call basis.  This base class provides something they can subclass to do that.  A reference is passed to all print()
 *  methods for semantic objects. */
class Formatter {
public:
    Formatter(): regdict(NULL), suppress_initial_values(false), indentation_suffix("  "), show_latest_writers(true),
                 show_properties(true) {}
    virtual ~Formatter() {}

    /** The register dictionary which is used for printing register names.
     * @{ */
    const RegisterDictionary *get_register_dictionary() const { return regdict; }
    void set_register_dictionary(const RegisterDictionary *rd) { regdict = rd; }
    /** @} */

    /** Whether register initial values should be suppressed.  If a register's value has a comment that is equal to the
     * register name with "_0" appended, then that value is assumed to be the register's initial value.
     * @{ */
    bool get_suppress_initial_values() const { return suppress_initial_values; }
    void set_suppress_initial_values(bool b=true) { suppress_initial_values=b; }
    void clear_suppress_initial_values() { set_suppress_initial_values(false); }
    /** @} */

    /** The string to print at the start of each line. This only applies to objects that occupy more than one line.
     * @{ */
    std::string get_line_prefix() const { return line_prefix; }
    void set_line_prefix(const std::string &s) { line_prefix = s; }
    /** @} */

    /** Indentation string appended to the line prefix for multi-level, multi-line outputs.
     * @{ */
    std::string get_indentation_suffix() const { return indentation_suffix; }
    void set_indentation_suffix(const std::string &s) { indentation_suffix = s; }
    /** @} */

    /** Whether to show latest writer information for register and memory states.
     * @{ */
    bool get_show_latest_writers() const { return show_latest_writers; }
    void set_show_latest_writers(bool b=true) { show_latest_writers = b; }
    void clear_show_latest_writers() { show_latest_writers = false; }
    /** @} */

    /** Whether to show register properties.
     * @{ */
    bool get_show_properties() const { return show_properties; }
    void set_show_properties(bool b=true) { show_properties = b; }
    void clear_show_properties() { show_properties = false; }
    /** @} */

protected:
    const RegisterDictionary *regdict;
    bool suppress_initial_values;
    std::string line_prefix;
    std::string indentation_suffix;
    bool show_latest_writers;
    bool show_properties;
};

/** Adjusts a Formatter for one additional level of indentation.  The formatter's line prefix is adjusted by appending the
 * formatter's indentation suffix.  When this Indent object is destructed, the formatter's line prefix is reset to its original
 * value. */
class Indent {
private:
    Formatter &fmt;
    std::string old_line_prefix;
public:
    Indent(Formatter &fmt): fmt(fmt) {
        old_line_prefix = fmt.get_line_prefix();
        fmt.set_line_prefix(old_line_prefix + fmt.get_indentation_suffix());
    }
    ~Indent() {
        fmt.set_line_prefix(old_line_prefix);
    }
};

/** Boolean properties related to I/O.
 *
 *  These Boolean properties keep track of whether a value was read from and/or written to a register or memory state.  Each
 *  state implementation has different capabilities, so see the implementation for details.  In short, @ref
 *  RegisterStateGeneric tracks these properties per bit of each register while memory states generally track them on a
 *  byte-by-byte basis.
 *
 *  Although the register and memory state objects provide the data members for storing this information, the properties are
 *  generally manipulated by higher layers such as the @c readRegister, @c writeRegister, @c readMemory, and @c writeMemory
 *  methods in a @ref BaseSemantics::RiscOperators "RiscOperators" implementation. */
enum InputOutputProperty {
    IO_READ,                                            /**< The location was read on behalf of an instruction. */
    IO_WRITE,                                           /**< The location was written on behalf of an instruction. */
    IO_INIT,                                            /**< The location was written without an instruction. This
                                                         *   typically happens during state initialization. */
    IO_READ_BEFORE_WRITE,                               /**< The location was read without having the IO_WRITE property. */
    IO_READ_AFTER_WRITE,                                /**< The location was read after being written. */
    IO_READ_UNINITIALIZED,                              /**< The location was read without having the IO_WRITE or IO_INIT
                                                         *   property. */
};

/** Set of Boolean properties. */
typedef Sawyer::Container::Set<InputOutputProperty> InputOutputPropertySet;



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                      Exceptions
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Base class for exceptions thrown by instruction semantics. */
class Exception: public Rose::Exception {
public:
    SgAsmInstruction *insn;
    Exception(const std::string &mesg, SgAsmInstruction *insn): Rose::Exception(mesg), insn(insn) {}
    void print(std::ostream&) const;
};

class NotImplemented: public Exception {
public:
    NotImplemented(const std::string &mesg, SgAsmInstruction *insn)
        : Exception(mesg, insn) {}
};



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                      Merging states
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Shared-ownership pointer for @ref Merger classes. See @ref heap_object_shared_ownership. */
typedef Sawyer::SharedPointer<class Merger> MergerPtr;

/** Controls state merge operations.
 *
 *  This is the base class for objects that control the details of merge operations. A merge of two semantic values or semantic
 *  states happens when control flow joins together in data-flow analysis, and perhaps other operations.  An optional @ref
 *  Merger object is passed as an argument into the merge functions and contains settings and other details that might be
 *  necessary during the merge operation.
 *
 *  The base classes for register state and memory state allow an optional @ref Merger object to be stored in the
 *  state. Whenever a state is copied, its merger object pointer is also copied (shallow copy of merger).  The merger object is
 *  passed as an argument to each call of @ref SValue::createMerged or @ref SValue::createOptionalMerge.  The user-defined
 *  versions of these functions can access the merger object to decide how to merge. For example, the symbolic domain defines a
 *  merger that controls whether merging two different semantic values results in bottom or a set containing both values.
 *
 *  @ref Merger objects are allocated on the heap and have shared ownership like most other instruction semantics
 *  objects. Therefore they have no public C++ constructors but instead use factory methods named "instance". Users should not
 *  explicitly delete these objects -- they will be deleted automatically. */
class Merger: public Sawyer::SharedObject {
    bool memoryAddressesMayAlias_;
    bool memoryMergeDebugging_;

protected:
    Merger(): memoryAddressesMayAlias_(true), memoryMergeDebugging_(false) {}

public:
    /** Shared ownership pointer for @ref Merger. See @ref heap_object_shared_ownership. */
    typedef MergerPtr Ptr;

    /** Allocating constructor. */
    static Ptr instance() {
        return Ptr(new Merger);
    }

    /** Whether memory addresses can alias one another.
     *
     *  If true and the memory state supports it, then merging of two states will check for addresses that can alias one
     *  another and adjust the merge accordingly.
     *
     * @{ */
    bool memoryAddressesMayAlias() const { return memoryAddressesMayAlias_; }
    void memoryAddressesMayAlias(bool b) { memoryAddressesMayAlias_ = b; }
    /** @} */

    /** Turn on output for memory merge debugging.
     *
     *  If set and the @c Rose::BinaryAnalysis::InstructionSemantics2 diagnostic stream is also enabled, then memory merge
     *  operations produce debugging diagnostics to that stream.  Memory merge debugging is not normally enabled even if the
     *  stream is enabled because it can produce a very large amount of output.
     *
     *  @{ */
    bool memoryMergeDebugging() const { return memoryMergeDebugging_; }
    void memoryMergeDebugging(bool b) { memoryMergeDebugging_ = b; }
    /** @} */
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                      Semantic Values
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// This is leftover for compatibility with an older API.  The old API had code like this:
//    User::SValue user_svalue = BaseSemantics::dynamic_pointer_cast<User::SValue>(base_svalue);
// Which can be replaced now with
//    User::SValue user_svalue = base_svalue.dynamicCast<User::SValue>();
template<class To, class From>
Sawyer::SharedPointer<To> dynamic_pointer_cast(const Sawyer::SharedPointer<From> &from) {
    return from.template dynamicCast<To>();
}

/** Shared-ownership pointer to a semantic value in any domain. See @ref heap_object_shared_ownership. */
typedef Sawyer::SharedPointer<class SValue> SValuePtr;

/** Base class for semantic values.
 *
 *  A semantic value represents a datum from the specimen being analyzed. The datum could be from memory, it could be something
 *  stored in a register, it could be the result of some computation, etc.  The datum in the specimen has a datum type that
 *  might be only partially known; the datum value could, for instance, be 32-bits but unknown whether it is integer or
 *  floating point.
 *
 *  The various semantic domains will define SValue subclasses that are appropriate for that domain--a concrete domain will
 *  define an SValue that specimen data in a concrete form, an interval domain will define an SValue that represents specimen
 *  data in intervals, etc.
 *
 *  Semantics value objects are allocated on the heap and reference counted.  The BaseSemantics::SValue is an abstract class
 *  that defines the interface.  See the Rose::BinaryAnalysis::InstructionSemantics2 namespace for an overview of how the parts
 *  fit together.*/
class SValue: public Sawyer::SharedObject, public Sawyer::SharedFromThis<SValue>, public Sawyer::SmallObject {
protected:
    size_t width;                               /** Width of the value in bits. Typically (not always) a power of two. */

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Serialization
#ifdef ROSE_HAVE_BOOST_SERIALIZATION_LIB
private:
    friend class boost::serialization::access;

    template<class S>
    void serialize(S &s, const unsigned /*version*/) {
        s & BOOST_SERIALIZATION_NVP(width);
    }
#endif
    
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Normal, protected, C++ constructors
protected:
    SValue(): width(0) {}                               // needed for serialization
    explicit SValue(size_t nbits): width(nbits) {}      // hot
    SValue(const SValue &other): Sawyer::SharedObject(other), width(other.width) {}

public:
    /** Shared-ownership pointer for an @ref SValue object. See @ref heap_object_shared_ownership. */
    typedef SValuePtr Ptr;

public:
    virtual ~SValue() {}

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Allocating static constructor.  None are needed--this class is abstract.

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Allocating virtual constructors.  undefined_() needs underscores, so we do so consistently for all
    // these allocating virtual c'tors.  However, we use copy() rather than copy_() because this one is fundamentally
    // different: the object (this) is use for more than just selecting which virtual method to invoke.
    //
    // The naming scheme we use here is a bit different than for most other objects for historical reasons.  Most other classes
    // use "create" and "clone" as the virtual constructor names, but SValue uses names ending in undercore, and "copy". The
    // other difference (at least in this base class) is that we don't define any real constructors or static allocating
    // constructors (usually named "instance")--it's because this is an abstract class.
public:
    /** Create a new undefined semantic value.  The new semantic value will have the same dynamic type as the value
     *  on which this virtual method is called.  This is the most common way that a new value is created. The @ref unspecified_
     *  method is closely related.
     *
     *  @sa unspecified_ */
    virtual SValuePtr undefined_(size_t nbits) const = 0; // hot

    /** Create a new unspecified semantic value. The new semantic value will have the same dynamic type as the value on which
     *  this virtual method is called.  Undefined (@ref undefined_) and unspecified are closely related.  Unspecified values
     *  are the same as undefined values except they're instantiated as the result of some machine instruction where the ISA
     *  documentation indicates that the value is unspecified (e.g., status flags for x86 shift and rotate instructions).
     *
     *  Most semantic domains make no distinction between undefined and unspecified.
     *
     *  @sa undefined_ */
    virtual SValuePtr unspecified_(size_t nbits) const = 0;

    /** Data-flow bottom value.
     *
     *  Returns a new value that represents bottom in a data-flow analysis. If a semantic domain can represent a bottom value
     *  then the @ref isBottom predicate is true when invoked on this method's return value. If a semantic domain cannot
     *  support a bottom value, then it may return some other value. */
    virtual SValuePtr bottom_(size_t nBits) const = 0;

    /** Create a new concrete semantic value. The new value will represent the specified concrete value and have the same
     *  dynamic type as the value on which this virtual method is called. This is the most common way that a new constant is
     *  created.  The @p number is truncated to contain @p nbits bits (higher order bits are cleared). */
    virtual SValuePtr number_(size_t nbits, uint64_t number) const = 0; // hot

    /** Create a new, Boolean value. The new semantic value will have the same dynamic type as the value on
     *  which this virtual method is called. This is how 1-bit flag register values (among others) are created. The base
     *  implementation uses number_() to construct a 1-bit value whose bit is zero (false) or one (true). */
    virtual SValuePtr boolean_(bool value) const { return number_(1, value?1:0); }

    /** Create a new value from an existing value, changing the width if @p new_width is non-zero. Increasing the width
     *  logically adds zero bits to the most significant side of the value; decreasing the width logically removes bits from the
     *  most significant side of the value. */
    virtual SValuePtr copy(size_t new_width=0) const = 0;

    /** Possibly create a new value by merging two existing values.
     *
     *  This method optionally returns a new semantic value as the data-flow merge of @p this and @p other.  If the two inputs
     *  are "equal" in some sense of the dataflow implementation then nothing is returned, otherwise a new value is returned.
     *  Typical usage is like this:
     *
     * @code
     *  if (SValuePtr merged = v1->createOptionalMerge(v2).orDefault()) {
     *      std::cout <<"v1 and v2 were merged to " <<*merged <<"\n";
     *  } else {
     *      std::cout <<"no merge is necessary\n";
     *  }
     *
     *  or
     *
     * @code
     *  SValuePtr merge;
     *  if (v1->createOptionalMerge(v2).assignTo(merged)) {
     *      std::cout <<"v1 and v2 were merged to " <<*merged <<"\n";
     *  } else {
     *      std::cout <<"v1 and v2 are equal in some sense (no merge necessary)\n";
     *  }
     * @endcode
     *
     *  If you always want a copy regardless of whether the merge is necessary, then use the @ref createMerged convenience
     *  function instead. */
    virtual Sawyer::Optional<SValuePtr>
    createOptionalMerge(const SValuePtr &other, const MergerPtr &merger, const SmtSolverPtr &solver) const = 0;

    /** Create a new value by merging two existing values.
     *
     *  This is a convenience wrapper around @ref createOptionalMerge. It always returns a newly constructed semantic value
     *  regardless of whether a merge was necessary.  In order to determine if a merge was necessary one can compare the
     *  return value to @p this using @ref must_equal, although doing so is more expensive than calling @ref
     *  createOptionalMerge. */
    SValuePtr createMerged(const SValuePtr &other, const MergerPtr &merger, const SmtSolverPtr &solver) const /*final*/ {
        return createOptionalMerge(other, merger, solver).orElse(copy());
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Dynamic pointer casts. No-ops since this is the base class
public:
    static SValuePtr promote(const SValuePtr &x) {
        ASSERT_not_null(x);
        return x;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // The rest of the API...
public:
    /** Determines whether a value is a data-flow bottom.
     *
     *  Returns true if this value represents a bottom value for data-flow analysis.  Any RiscOperation performed on an operand
     *  whose isBottom predicate returns true will itself return a bottom value.  This includes operations like "xor x x" which
     *  would normally return zero. */
    virtual bool isBottom() const = 0;

    /** Determines if the value is a concrete number. Concrete numbers can be created with the number_(), boolean_()
     *  virtual constructors, or by other means. */
    virtual bool is_number() const = 0;

    /** Return the concrete number for this value.  Only values for which is_number() returns true are able to return a
     *  concrete value by this method. */
    virtual uint64_t get_number() const = 0;

    /** Accessor for value width.
     * @{ */
    virtual size_t get_width() const { return width; }
    virtual void set_width(size_t nbits) { width = nbits; }
    /** @} */

    /** Returns true if two values could be equal. The SMT solver is optional for many subclasses. */
    virtual bool may_equal(const SValuePtr &other, const SmtSolverPtr &solver = SmtSolverPtr()) const = 0;

    /** Returns true if two values must be equal.  The SMT solver is optional for many subclasses. */
    virtual bool must_equal(const SValuePtr &other, const SmtSolverPtr &solver = SmtSolverPtr()) const = 0;

    /** Returns true if concrete non-zero. This is not virtual since it can be implemented in terms of @ref is_number and @ref
     *  get_number. */
    bool isTrue() const {
        return is_number() && get_number()!=0;
    }

    /** Returns true if concrete zero.  This is not virtual since it can be implemented in terms of @ref is_number and @ref
     *  get_number. */
    bool isFalse() const {
        return is_number() && get_number()==0;
    }

    /** Print a value to a stream using default format. The value will normally occupy a single line and not contain leading
     *  space or line termination.  See also, with_format().
     *  @{ */
    void print(std::ostream &stream) const { Formatter fmt; print(stream, fmt); }
    virtual void print(std::ostream&, Formatter&) const = 0;
    /** @} */

    /** SValue with formatter. See with_formatter(). */
    class WithFormatter {
        SValuePtr obj;
        Formatter &fmt;
    public:
        WithFormatter(const SValuePtr &svalue, Formatter &fmt): obj(svalue), fmt(fmt) {}
        void print(std::ostream &stream) const { obj->print(stream, fmt); }
    };

    /** Used for printing values with formatting. The usual way to use this is:
     * @code
     *  SValuePtr val = ...;
     *  Formatter fmt = ...;
     *  std::cout <<"The value is: " <<(*val+fmt) <<"\n";
     * @endcode
     * @{ */
    WithFormatter with_format(Formatter &fmt) { return WithFormatter(SValuePtr(this), fmt); }
    WithFormatter operator+(Formatter &fmt) { return with_format(fmt); }
    /** @} */
    
    /** Some subclasses support the ability to add comments to values. We define no-op versions of these methods here
     *  because it makes things easier.  The base class tries to be as small as possible by not storing comments at
     *  all. Comments should not affect any computation (comparisons, hash values, etc), and therefore are allowed to be
     *  modified even for const objects.
     * @{ */
    virtual std::string get_comment() const { return ""; }
    virtual void set_comment(const std::string&) const {} // const is intended; cf. doxygen comment
    /** @} */
};



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                      Register States
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Shared-ownership pointer to a register state. See @ref heap_object_shared_ownership. */
typedef boost::shared_ptr<class RegisterState> RegisterStatePtr;

/** The set of all registers and their values. RegisterState objects are allocated on the heap and reference counted.  The
 *  BaseSemantics::RegisterState is an abstract class that defines the interface.  See the
 *  Rose::BinaryAnalysis::InstructionSemantics2 namespace for an overview of how the parts fit together.*/
class RegisterState: public boost::enable_shared_from_this<RegisterState> {
private:
    MergerPtr merger_;
    SValuePtr protoval_;                                /**< Prototypical value for virtual constructors. */

protected:
    const RegisterDictionary *regdict;                  /**< Registers that are able to be stored by this state. */

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Serialization
#ifdef ROSE_HAVE_BOOST_SERIALIZATION_LIB
private:
    friend class boost::serialization::access;

    template<class S>
    void serialize(S &s, const unsigned version) {
        //s & merger_; -- not saved
        s & BOOST_SERIALIZATION_NVP(protoval_);
        if (version >= 1)
            s & BOOST_SERIALIZATION_NVP(regdict);
    }
#endif

    
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Real constructors
protected:
    RegisterState()
        : regdict(NULL) {}                                // for serialization

    RegisterState(const SValuePtr &protoval, const RegisterDictionary *regdict)
        : protoval_(protoval), regdict(regdict) {
        ASSERT_not_null(protoval_);
    }

public:
    /** Shared-ownership pointer for a @ref RegisterState object. See @ref heap_object_shared_ownership. */
    typedef RegisterStatePtr Ptr;

public:
    virtual ~RegisterState() {}

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Static allocating constructors.  None are needed--this class is abstract.


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Virtual constructors.
public:
    /** Virtual constructor.  The @p protoval argument must be a non-null pointer to a semantic value which will be used only
     *  to create additional instances of the value via its virtual constructors.  The prototypical value is normally of the
     *  same type for all parts of a semantic analysis. The register state must be compatible with the rest of the binary
     *  analysis objects in use. */
    virtual RegisterStatePtr create(const SValuePtr &protoval, const RegisterDictionary *regdict) const = 0;

    /** Make a copy of this register state. */
    virtual RegisterStatePtr clone() const = 0;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Dynamic pointer casts. No-op since this is the base class.
public:
    static RegisterStatePtr promote(const RegisterStatePtr &x) {
        ASSERT_not_null(x);
        return x;
    }

public:
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // The rest of the API...

    /** Property: Merger.
     *
     *  This property is optional details about how to merge two states. It is passed down to the register and memory state
     *  merge operation and to the semantic value merge operation.  Users can subclass this to hold whatever information is
     *  necessary for merging.  Unless the user overrides merge functions to do something else, all merging will use the same
     *  merger object -- the one set for this property.
     *
     * @{ */
    MergerPtr merger() const { return merger_; }
    void merger(const MergerPtr &m) { merger_ = m; }
    /** @} */

    /** Return the protoval.  The protoval is used to construct other values via its virtual constructors. */
    SValuePtr protoval() const { return protoval_; }

    /** The register dictionary should be compatible with the register dictionary used for other parts of binary analysis. At
     *  this time (May 2013) the dictionary is only used when printing.
     * @{ */
    const RegisterDictionary *get_register_dictionary() const { return regdict; }
    void set_register_dictionary(const RegisterDictionary *rd) { regdict = rd; }
    /** @} */

    /** Removes stored values from the register state.
     *
     *  Depending on the register state implementation, this could either store new, distinct undefined values in each
     *  register, or it could simply erase all information about stored values leaving the register state truly empty. For
     *  instance RegisterStateGeneric, which uses variable length arrays to store information about a dynamically changing set
     *  of registers, clears its arrays to zero length.
     *
     *  Register states can also be initialized by clearing them or by explicitly writing new values into each desired
     *  register (or both). See @ref RegisterStateGeneric::initialize_nonoverlapping for one way to initialize that register
     *  state. */
    virtual void clear() = 0;

    /** Set all registers to the zero. */
    virtual void zero() = 0;

    /** Merge register states for data flow analysis.
     *
     *  Merges the @p other state into this state, returning true if this state changed. */
    virtual bool merge(const RegisterStatePtr &other, RiscOperators *ops) = 0;

    /** Read a value from a register.
     *
     *  The register descriptor, @p reg, not only describes which register, but also which bits of that register (e.g., "al",
     *  "ah", "ax", "eax", and "rax" are all the same hardware register on an amd64, but refer to different parts of that
     *  register). The RISC operations are provided so that they can be used to extract the correct bits from a wider hardware
     *  register if necessary.
     *
     *  The @p dflt value is written into the register state if the register was not defined in the state. By doing this, a
     *  subsequent read of the same register will return the same value. Some register states cannot distinguish between a
     *  register that was never accessed and a register that was only read, in which case @p dflt is not used since all
     *  registers are already initialized.
     *
     *  See @ref RiscOperators::readRegister for more details. */
    virtual SValuePtr readRegister(RegisterDescriptor reg, const SValuePtr &dflt, RiscOperators *ops) = 0;

    /** Read a register without side effects.
     *
     *  This is similar to @ref readRegister except it doesn't modify the register state in any way. */
    virtual SValuePtr peekRegister(RegisterDescriptor reg, const SValuePtr &dflt, RiscOperators *ops) = 0;

    /** Write a value to a register.
     *
     *  The register descriptor, @p reg, not only describes which register, but also which bits of that register (e.g., "al",
     *  "ah", "ax", "eax", and "rax" are all the same hardware register on an amd64, but refer to different parts of that
     *  register). The RISC operations are provided so that they can be used to insert the @p value bits into a wider the
     *  hardware register if necessary. See @ref RiscOperators::readRegister for more details. */
    virtual void writeRegister(RegisterDescriptor reg, const SValuePtr &value, RiscOperators *ops) = 0;

    /** Print the register contents. This emits one line per register and contains the register name and its value.
     *  @{ */
    void print(std::ostream &stream, const std::string prefix="") const {
        Formatter fmt;
        fmt.set_line_prefix(prefix);
        print(stream, fmt);
    }
    virtual void print(std::ostream&, Formatter&) const = 0;
    /** @} */

    /** RegisterState with formatter. See with_formatter(). */
    class WithFormatter {
        RegisterStatePtr obj;
        Formatter &fmt;
    public:
        WithFormatter(const RegisterStatePtr &obj, Formatter &fmt): obj(obj), fmt(fmt) {}
        void print(std::ostream &stream) const { obj->print(stream, fmt); }
    };

    /** Used for printing register states with formatting. The usual way to use this is:
     * @code
     *  RegisterStatePtr obj = ...;
     *  Formatter fmt = ...;
     *  std::cout <<"The value is: " <<(*obj+fmt) <<"\n";
     * @endcode
     * @{ */
    WithFormatter with_format(Formatter &fmt) { return WithFormatter(shared_from_this(), fmt); }
    WithFormatter operator+(Formatter &fmt) { return with_format(fmt); }
    /** @} */

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                      Memory State
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Shared-ownership pointer to a memory state. See @ref heap_object_shared_ownership. */
typedef boost::shared_ptr<class MemoryState> MemoryStatePtr;

/** Represents all memory in the state. MemoryState objects are allocated on the heap and reference counted.  The
 *  BaseSemantics::MemoryState is an abstract class that defines the interface.  See the
 *  Rose::BinaryAnalysis::InstructionSemantics2 namespace for an overview of how the parts fit together.*/
class MemoryState: public boost::enable_shared_from_this<MemoryState> {
    SValuePtr addrProtoval_;                            /**< Prototypical value for addresses. */
    SValuePtr valProtoval_;                             /**< Prototypical value for values. */
    ByteOrder::Endianness byteOrder_;                   /**< Memory byte order. */
    MergerPtr merger_;
    bool byteRestricted_;                               // are cell values all exactly one byte wide?

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Serialization
#ifdef ROSE_HAVE_BOOST_SERIALIZATION_LIB
private:
    friend class boost::serialization::access;

    template<class S>
    void serialize(S &s, const unsigned /*version*/) {
        s & BOOST_SERIALIZATION_NVP(addrProtoval_);
        s & BOOST_SERIALIZATION_NVP(valProtoval_);
        s & BOOST_SERIALIZATION_NVP(byteOrder_);
        //s & merger_ -- not saved
        s & BOOST_SERIALIZATION_NVP(byteRestricted_);
    }
#endif


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Real constructors
protected:
    MemoryState()                                       // for serialization
        : byteOrder_(ByteOrder::ORDER_UNSPECIFIED), byteRestricted_(true) {}

    explicit MemoryState(const SValuePtr &addrProtoval, const SValuePtr &valProtoval)
        : addrProtoval_(addrProtoval), valProtoval_(valProtoval), byteOrder_(ByteOrder::ORDER_UNSPECIFIED),
          byteRestricted_(true) {
        ASSERT_not_null(addrProtoval);
        ASSERT_not_null(valProtoval);
    }

    MemoryState(const MemoryStatePtr &other)
        : addrProtoval_(other->addrProtoval_), valProtoval_(other->valProtoval_), byteOrder_(ByteOrder::ORDER_UNSPECIFIED),
          merger_(other->merger_), byteRestricted_(other->byteRestricted_) {}

public:
    /** Shared-ownership pointer for a @ref MemoryState. See @ref heap_object_shared_ownership. */
    typedef MemoryStatePtr Ptr;

public:
    virtual ~MemoryState() {}

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Static allocating constructors. None needed since this class is abstract

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Virtual constructors
public:
    /** Virtual allocating constructor.
     *
     *  Allocates and constructs a new MemoryState object having the same dynamic type as this object. A prototypical SValue
     *  must be supplied and will be used to construct any additional SValue objects needed during the operation of a
     *  MemoryState.  Two prototypical values are supplied, one for addresses and another for values stored at those addresses,
     *  although they will almost always be the same. */
    virtual MemoryStatePtr create(const SValuePtr &addrProtoval, const SValuePtr &valProtoval) const = 0;

    /** Virtual allocating copy constructor. Creates a new MemoryState object which is a copy of this object. */
    virtual MemoryStatePtr clone() const = 0;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Dynamic pointer casts.  No-op since this is the base class.
public:
    static MemoryStatePtr promote(const MemoryStatePtr &x) {
        ASSERT_not_null(x);
        return x;
    }
    
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Methods first declared at this level of the class hierarchy
public:
    /** Property: Merger.
     *
     *  This property is optional details about how to merge two states. It is passed down to the register and memory state
     *  merge operation and to the semantic value merge operation.  Users can subclass this to hold whatever information is
     *  necessary for merging.  Unless the user overrides merge functions to do something else, all merging will use the same
     *  merger object -- the one set for this property.
     *
     * @{ */
    MergerPtr merger() const { return merger_; }
    void merger(const MergerPtr &m) { merger_ = m; }
    /** @} */

    /** Return the address protoval.  The address protoval is used to construct other memory addresses via its virtual
     *  constructors. */
    SValuePtr get_addr_protoval() const { return addrProtoval_; }

    /** Return the value protoval.  The value protoval is used to construct other stored values via its virtual
     *  constructors. */
    SValuePtr get_val_protoval() const { return valProtoval_; }

    /** Clear memory. Removes all memory cells from this memory state. */
    virtual void clear() = 0;

    /** Indicates whether memory cell values are required to be eight bits wide.
     *
     *  The default is true since this simplifies the calculations for whether two memory cells are alias and how to combine
     *  the value from two or more aliasing cells. A memory that contains only eight-bit values requires that the caller
     *  concatenate/extract individual bytes when reading/writing multi-byte values.
     *
     * @{ */
    bool byteRestricted() const { return byteRestricted_; }
    void byteRestricted(bool b) { byteRestricted_ = b; }
    /** @} */

    /** Memory byte order.
     *  @{ */
    ByteOrder::Endianness get_byteOrder() const { return byteOrder_; }
    void set_byteOrder(ByteOrder::Endianness bo) { byteOrder_ = bo; }
    /** @} */

    /** Merge memory states for data flow analysis.
     *
     *  Merges the @p other state into this state, returning true if this state changed. */
    virtual bool merge(const MemoryStatePtr &other, RiscOperators *addrOps, RiscOperators *valOps) = 0;

    /** Read a value from memory.
     *
     *  Consults the memory represented by this MemoryState object and returns a semantic value. Depending on the semantic
     *  domain, the value can be a value that is already stored in the memory state, a supplied default value, a new value
     *  constructed from some combination of existing values and/or the default value, or anything else.  For instance, in a
     *  symbolic domain the @p address could alias multiple existing memory locations and the implementation may choose to
     *  return a McCarthy expression.  Additional data (such as SMT solvers) may be passed via the RiscOperators argument.
     *
     *  The size of the value being read does not necessarily need to be equal to the size of values stored in the memory
     *  state, though it typically is(1). For instance, an implementation may allow reading a 32-bit little endian value from a
     *  memory state that stores only bytes.  A RiscOperators object is provided for use in these situations.
     *
     *  In order to support cases where an address does not match any existing location, the @p dflt value can be used to
     *  initialize a new memory location.  The manner in which the default is used depends on the implementation.  In any case,
     *  the width of the @p dflt value determines how much to read.
     *
     *  Footnote 1: A MemoryState::readMemory() call is the last in a sequence of delegations starting with
     *  RiscOperators::readMemory().  The designers of the MemoryState, State, and RiscOperators subclasses will need to
     *  coordinate to decide which layer should handle concatenating values from individual memory locations. */
    virtual SValuePtr readMemory(const SValuePtr &address, const SValuePtr &dflt,
                                 RiscOperators *addrOps, RiscOperators *valOps) = 0;

    /** Read a value from memory without side effects.
     *
     *  This is similar to @ref readMemory except there are no side effects. The memory state is not modified by this
     *  function. */
    virtual SValuePtr peekMemory(const SValuePtr &address, const SValuePtr &dflt,
                                 RiscOperators *addrOps, RiscOperators *valOps) = 0;

    /** Write a value to memory.
     *
     *  Consults the memory represented by this MemoryState object and possibly inserts the specified value.  The details of
     *  how a value is inserted into a memory state depends entirely on the implementation in a subclass and will probably be
     *  different for each semantic domain.
     *
     *  A MemoryState::writeMemory() call is the last in a sequence of delegations starting with
     *  RiscOperators::writeMemory(). The designers of the MemoryState, State, and RiscOperators will need to coordinate to
     *  decide which layer (if any) should handle splitting a multi-byte value into multiple memory locations. */
    virtual void writeMemory(const SValuePtr &addr, const SValuePtr &value,
                             RiscOperators *addrOps, RiscOperators *valOps) = 0;

    /** Print a memory state to more than one line of output.
     * @{ */
    void print(std::ostream &stream, const std::string prefix="") const {
        Formatter fmt;
        fmt.set_line_prefix(prefix);
        print(stream, fmt);
    }
    virtual void print(std::ostream&, Formatter&) const = 0;
    /** @} */

    /** MemoryState with formatter. See with_formatter(). */
    class WithFormatter {
        MemoryStatePtr obj;
        Formatter &fmt;
    public:
        WithFormatter(const MemoryStatePtr &obj, Formatter &fmt): obj(obj), fmt(fmt) {}
        void print(std::ostream &stream) const { obj->print(stream, fmt); }
    };

    /** Used for printing memory states with formatting. The usual way to use this is:
     * @code
     *  MemoryStatePtr obj = ...;
     *  Formatter fmt = ...;
     *  std::cout <<"The value is: " <<(*obj+fmt) <<"\n";
     * @endcode
     * @{ */
    WithFormatter with_format(Formatter &fmt) { return WithFormatter(shared_from_this(), fmt); }
    WithFormatter operator+(Formatter &fmt) { return with_format(fmt); }
    /** @} */
};



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                      State
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Shared-ownership pointer to a semantic state. See @ref heap_object_shared_ownership. */
typedef boost::shared_ptr<class State> StatePtr;

/** Base class for semantics machine states.
 *
 *  Binary semantic analysis usually progresses one instruction at a time--one starts with an initial state and the act of
 *  processing an instruction modifies the state.  The State is the base class class for the semantic states of various
 *  instruction semantic policies.  It contains storage for all the machine registers and memory.
 *
 *  Sometimes it's useful to have a state that contains only registers or only memory.  Although this class doesn't allow its
 *  register or memory state children to be null pointers, the @ref NullSemantics class provides register and memory states
 *  that are mostly no-ops.
 *
 *  States must be copyable objects.  Many analyses keep a copy of the machine state for each instruction or each CFG
 *  vertex.
 *
 *  State objects are allocated on the heap and reference counted.  The BaseSemantics::State is an abstract class that defines
 *  the interface.  See the Rose::BinaryAnalysis::InstructionSemantics2 namespace for an overview of how the parts fit
 *  together.  */
class State: public boost::enable_shared_from_this<State> {
    SValuePtr protoval_;                                // Initial value used to create additional values as needed.
    RegisterStatePtr registers_;                        // All machine register values for this semantic state.
    MemoryStatePtr memory_;                             // All memory for this semantic state.

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Serialization
#ifdef ROSE_HAVE_BOOST_SERIALIZATION_LIB
private:
    friend class boost::serialization::access;

    template<class S>
    void serialize(S &s, const unsigned /*version*/) {
        s & BOOST_SERIALIZATION_NVP(protoval_);
        s & BOOST_SERIALIZATION_NVP(registers_);
        s & BOOST_SERIALIZATION_NVP(memory_);
    }
#endif


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Real constructors
protected:
    // needed for serialization
    State() {}

    State(const RegisterStatePtr &registers, const MemoryStatePtr &memory)
        : registers_(registers), memory_(memory) {
        ASSERT_not_null(registers);
        ASSERT_not_null(memory);
        protoval_ = registers->protoval();
        ASSERT_not_null(protoval_);
    }

    // deep-copy the registers and memory
    State(const State &other)
        : boost::enable_shared_from_this<State>(other), protoval_(other.protoval_) {
        registers_ = other.registers_->clone();
        memory_ = other.memory_->clone();
    }

public:
    /** Shared-ownership pointer for a @ref State. See @ref heap_object_shared_ownership. */
    typedef StatePtr Ptr;

public:
    virtual ~State() {}

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Static allocating constructors
public:
    /** Instantiate a new state object with specified register and memory states. */
    static StatePtr instance(const RegisterStatePtr &registers, const MemoryStatePtr &memory) {
        return StatePtr(new State(registers, memory));
    }

    /** Instantiate a new copy of an existing state. */
    static StatePtr instance(const StatePtr &other) {
        return StatePtr(new State(*other));
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Virtual constructors
public:
    /** Virtual constructor. */
    virtual StatePtr create(const RegisterStatePtr &registers, const MemoryStatePtr &memory) const {
        return instance(registers, memory);
    }

    /** Virtual copy constructor. Allocates a new state object which is a deep copy of this state. States must be copyable
     *  objects because many analyses depend on being able to make a copy of the entire semantic state at each machine
     *  instruction, at each CFG vertex, etc. */
    virtual StatePtr clone() const {
        StatePtr self = boost::const_pointer_cast<State>(shared_from_this());
        return instance(self);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Dynamic pointer casts.  No-op since this is the base class.
public:
    static StatePtr promote(const StatePtr &x) {
        ASSERT_not_null(x);
        return x;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Other methods that are part of our API. Most of these just chain to either the register state and/or the memory state.
public:
    /** Return the protoval.  The protoval is used to construct other values via its virtual constructors. */
    SValuePtr protoval() const { return protoval_; }

    /** Initialize state.  The register and memory states are cleared. */
    virtual void clear();

    /** Initialize all registers to zero.
     *
     *  Calls the @ref RegisterState::zero method. Memory is not affected. */
    virtual void zero_registers();

    /** Clear all memory locations.
     *
     *  Calls the @ref MemoryState::clear method. Registers are not affected. */
    virtual void clear_memory();

    /** Property: Register state.
     *
     *  This read-only property is the register substate of this whole state. */
    RegisterStatePtr registerState() const {
        return registers_;
    }

    /** Property: Memory state.
     *
     *  This read-only property is the memory substate of this whole state. */
    MemoryStatePtr memoryState() const {
        return memory_;
    }

    /** Read a value from a register.
     *
     *  The @ref BaseSemantics::readRegister implementation simply delegates to the register state member of this state.  See
     *  @ref BaseSemantics::RiscOperators::readRegister for details. */
    virtual SValuePtr readRegister(RegisterDescriptor desc, const SValuePtr &dflt, RiscOperators *ops);

    /** Read register without side effects.
     *
     *  The @ref BaseSemantics::peekRegister implementation simply delegates to the register state member of this state.  See
     *  @ref BaseSemantics::RiscOperators::peekRegister for details. */
    virtual SValuePtr peekRegister(RegisterDescriptor desc, const SValuePtr &dflt, RiscOperators *ops);

    /** Write a value to a register.
     *
     *  The @ref BaseSemantics::writeRegister implementation simply delegates to the register state member of this state.  See
     *  @ref BaseSemantics::RiscOperators::writeRegister for details. */
    virtual void writeRegister(RegisterDescriptor desc, const SValuePtr &value, RiscOperators *ops);

    /** Read a value from memory.
     *
     *  The BaseSemantics::readMemory() implementation simply delegates to the memory state member of this state.  See
     *  BaseSemantics::RiscOperators::readMemory() for details.  */
    virtual SValuePtr readMemory(const SValuePtr &address, const SValuePtr &dflt,
                                 RiscOperators *addrOps, RiscOperators *valOps);

    /** Read from memory without side effects.
     *
     *  The BaseSemantics::peekMemory() implementation simply delegates to the memory state member of this state.  See
     *  BaseSemantics::RiscOperators::peekMemory() for details.  */
    virtual SValuePtr peekMemory(const SValuePtr &address, const SValuePtr &dflt,
                                 RiscOperators *addrOps, RiscOperators *valOps);

    /** Write a value to memory.
     *
     *  The BaseSemantics::writeMemory() implementation simply delegates to the memory state member of this state.  See
     *  BaseSemantics::RiscOperators::writeMemory() for details. */
    virtual void writeMemory(const SValuePtr &addr, const SValuePtr &value, RiscOperators *addrOps, RiscOperators *valOps);

    /** Print the register contents.
     *
     *  This method emits one line per register and contains the register name and its value.
     *
     * @{ */
    void printRegisters(std::ostream &stream, const std::string &prefix = "");
    virtual void printRegisters(std::ostream &stream, Formatter &fmt) const;
    /** @} */

    /** Print memory contents.
     *
     *  This simply calls the MemoryState::print method.
     *
     * @{ */
    void printMemory(std::ostream &stream, const std::string &prefix = "") const;
    virtual void printMemory(std::ostream &stream, Formatter &fmt) const;
    /** @} */

    /** Print the state.  This emits a multi-line string containing the registers and all known memory locations.
     * @{ */
    void print(std::ostream &stream, const std::string &prefix = "") const;
    virtual void print(std::ostream&, Formatter&) const;
    /** @} */

    /** State with formatter. See with_formatter(). */
    class WithFormatter {
        StatePtr obj;
        Formatter &fmt;
    public:
        WithFormatter(const StatePtr &obj, Formatter &fmt): obj(obj), fmt(fmt) {}
        void print(std::ostream &stream) const { obj->print(stream, fmt); }
    };

    /** Used for printing states with formatting. The usual way to use this is:
     * @code
     *  StatePtr obj = ...;
     *  Formatter fmt = ...;
     *  std::cout <<"The value is: " <<(*obj+fmt) <<"\n";
     * @endcode
     * @{ */
    WithFormatter with_format(Formatter &fmt) { return WithFormatter(shared_from_this(), fmt); }
    WithFormatter operator+(Formatter &fmt) { return with_format(fmt); }
    /** @} */

    /** Merge operation for data flow analysis.
     *
     *  Merges the @p other state into this state. Returns true if this state changed, false otherwise.  This method usually
     *  isn't overridden in subclasses since all the base implementation does is invoke the merge operation on the memory state
     *  and register state. */
    virtual bool merge(const StatePtr &other, RiscOperators *ops);
};



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                      RISC Operators
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Shared-ownership pointer to a RISC operators object. See @ref heap_object_shared_ownership. */
typedef boost::shared_ptr<class RiscOperators> RiscOperatorsPtr;

/** Base class for most instruction semantics RISC operators.
 *
 *  This class is responsible for defining the semantics of the RISC-like operations invoked by the translation object (e.g.,
 *  X86InstructionSemantics).  We omit the definitions for most of the RISC operations from the base class so that failure to
 *  implement them in a subclass is an error.
 *
 *  RISC operator arguments are, in general, SValue pointers.  However, if the width of a RISC operator's result depends on an
 *  argument's value (as opposed to depending on the argument width), then that argument must be a concrete value (i.e., an
 *  integral type).  This requirement is due to the fact that SMT solvers need to know the sizes of their bit
 *  vectors. Operators @ref extract, @ref unsignedExtend, @ref signExtend, @ref readRegister, and @ref readMemory fall into
 *  this category.
 *
 *  Operators with side effects (@ref writeRegister, @ref writeMemory, and possibly others) usually modify a @ref State object
 *  pointed to by the @ref currentState property. Keeping side effects in states allows @ref RiscOperators to be used in
 *  data-flow analysis where meeting control flow edges cause states to be merged.  Side effects that don't need to be part of
 *  a data-flow can be stored elsewhere, such as data members of a subclass or the @ref initialState property.
 *
 *  RiscOperator objects are allocated on the heap and reference counted.  The BaseSemantics::RiscOperator is an abstract class
 *  that defines the interface.  See the Rose::BinaryAnalysis::InstructionSemantics2 namespace for an overview of how the parts
 *  fit together. */
class RiscOperators: public boost::enable_shared_from_this<RiscOperators> {
    SValuePtr protoval_;                                // Prototypical value used for its virtual constructors
    StatePtr currentState_;                             // State upon which RISC operators operate
    StatePtr initialState_;                             // Lazily updated initial state; see readMemory
    SmtSolverPtr solver_;                               // Optional SMT solver
    SgAsmInstruction *currentInsn_;                     // Current instruction, as set by latest startInstruction call
    size_t nInsns_;                                     // Number of instructions processed
    std::string name_;                                  // Name to use for debugging

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Serialization
#ifdef ROSE_HAVE_BOOST_SERIALIZATION_LIB
private:
    friend class boost::serialization::access;

    template<class S>
    void serialize(S &s, const unsigned /*version*/) {
        s & BOOST_SERIALIZATION_NVP(protoval_);
        s & BOOST_SERIALIZATION_NVP(currentState_);
        s & BOOST_SERIALIZATION_NVP(initialState_);
        s & BOOST_SERIALIZATION_NVP(solver_);
        s & BOOST_SERIALIZATION_NVP(currentInsn_);
        s & BOOST_SERIALIZATION_NVP(nInsns_);
        s & BOOST_SERIALIZATION_NVP(name_);
    }
#endif

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Real constructors
protected:
    // for serialization
    RiscOperators()
        : currentInsn_(NULL), nInsns_(0) {}

    explicit RiscOperators(const SValuePtr &protoval, const SmtSolverPtr &solver = SmtSolverPtr())
        : protoval_(protoval), solver_(solver), currentInsn_(NULL), nInsns_(0) {
        ASSERT_not_null(protoval_);
    }

    explicit RiscOperators(const StatePtr &state, const SmtSolverPtr &solver = SmtSolverPtr())
        : currentState_(state), solver_(solver), currentInsn_(NULL), nInsns_(0) {
        ASSERT_not_null(state);
        protoval_ = state->protoval();
    }

public:
    /** Shared-ownership pointer for a @ref RiscOperators object. See @ref heap_object_shared_ownership. */
    typedef RiscOperatorsPtr Ptr;

public:
    virtual ~RiscOperators() {}

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Static allocating constructors.  None needed since this class is abstract.


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Virtual constructors.
public:
    /** Virtual allocating constructor.  The @p protoval is a prototypical semantic value that is used as a factory to create
     *  additional values as necessary via its virtual constructors.  The state upon which the RISC operations operate must be
     *  set by modifying the  @ref currentState property. An optional SMT solver may be specified (see @ref solver). */
    virtual RiscOperatorsPtr create(const SValuePtr &protoval, const SmtSolverPtr &solver = SmtSolverPtr()) const = 0;

    /** Virtual allocating constructor.  The supplied @p state is that upon which the RISC operations operate and is also used
     *  to define the prototypical semantic value. Other states can be supplied by setting @ref currentState. The prototypical
     *  semantic value is used as a factory to create additional values as necessary via its virtual constructors. An optional
     *  SMT solver may be specified (see @ref solver). */
    virtual RiscOperatorsPtr create(const StatePtr &state, const SmtSolverPtr &solver = SmtSolverPtr()) const = 0;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Dynamic pointer casts.  No-op since this is the base class.
public:
    static RiscOperatorsPtr promote(const RiscOperatorsPtr &x) {
        ASSERT_not_null(x);
        return x;
    }
    
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Other methods part of our API
public:
    /** Property: Prototypical semantic value.
     *
     *  The protoval is used to construct other values via its virtual constructors. */
    virtual SValuePtr protoval() const { return protoval_; }

    /** Property: Satisfiability module theory (SMT) solver.
     *
     *  This property holds a pointer to the satisfiability modulo theory (SMT) solver to use for certain operations.  An SMT
     *  solver is optional and not all semantic domains will make use of a solver.  Domains that use a solver will fall back to
     *  naive implementations when a solver is not available (for instance, equality of two values might be checked by looking
     *  at whether the values are identical).
     *
     * @{ */
    virtual SmtSolverPtr solver() const { return solver_; }
    virtual void solver(const SmtSolverPtr &s) { solver_ = s; }
    /** @} */

    /** Property: Current semantic state.
     *
     *  This is the state upon which the RISC operations operate. The state need not be set until the first instruction is
     *  executed (and even then, some RISC operations don't need any machine state; typically, only register and memory read
     *  and write operators need state).  Different state objects can be swapped in at pretty much any time.  Modifying the
     *  state has no effect on this object's prototypical value which was initialized by the constructor; new states should
     *  have a prototyipcal value of the same dynamic type.
     *
     *  See also, @ref initialState.
     *
     * @{ */
    virtual StatePtr currentState() const { return currentState_; }
    virtual void currentState(const StatePtr &s) { currentState_ = s; }
    /** @} */

    /** Property: Optional lazily updated initial state.
     *
     *  If non-null, then any calls to @ref readMemory or @ref readRegister which do not find that the address or register has
     *  a value, not only instantiate the value in the current state, but also write the same value to this initial state.  In
     *  effect, this is like Schrodinger's cat: every memory address and register has a value, we just don't know what it is
     *  until we try to read it.  Once we read it, it becomes instantiated in the current state and the initial state. The
     *  default initial state is the null pointer.
     *
     *  Changing the current state does not affect the initial state.  This makes it easier to use a state as part of a
     *  data-flow analysis, in which one typically swaps in different current states as the data-flow progresses.
     *
     *  The initial state need not be the same type as the current state, as long as they both have the same prototypical value
     *  type.  For instance, a symbolic domain could use a @ref MemoryCellList for its @ref currentState and a state based
     *  on a @ref MemoryMap of concrete values for its initial state, as long as those concrete values are converted to
     *  symbolic values when they're read.
     *
     *  <b>Caveats:</b> Not all semantic domains use the initial state. The order that values are added to an initial state
     *  depends on the order they're encountered during the analysis.
     *
     *  See also, @ref currentState.
     *
     *  @section example1 Example 1: Simple usage
     *
     *  This example, shows one way to use an initial state and the effect is has on memory and register I/O. It uses the same
     *  type for the initial state as it does for the current states.
     *
     *  @snippet testLazyInitialStates.C basicReadTest
     *
     *  @section example2 Example 2: Advanced usage
     *
     *  This example is somwewhat more advanced. It uses a custom state, which is a relatively common practice of users, and
     *  augments it to do something special when it's used as an initial state. When it's used as an initial state, it sets a
     *  flag for the values produced so that an analysis can presumably detect that the value is an initial value.
     *
     *  @snippet testLazyInitialStates.C advancedReadTest
     *
     * @{ */
    virtual StatePtr initialState() const { return initialState_; }
    virtual void initialState(const StatePtr &s) { initialState_ = s; }
    /** @} */

    /** Property: Name used for debugging.
     *
     *  This property is the name of the semantic domain and is used in diagnostic messages.
     *
     * @{ */
    virtual const std::string& name() const { return name_; }
    virtual void name(const std::string &s) { name_ = s; }
    /** @} */

    /** Print multi-line output for this object.
     * @{ */
    void print(std::ostream &stream, const std::string prefix="") const {
        Formatter fmt;
        fmt.set_line_prefix(prefix);
        print(stream, fmt);
    }
    virtual void print(std::ostream &stream, Formatter &fmt) const {
        currentState_->print(stream, fmt);
    }
    /** @} */

    /** RiscOperators with formatter. See with_formatter(). */
    class WithFormatter {
        RiscOperatorsPtr obj;
        Formatter &fmt;
    public:
        WithFormatter(const RiscOperatorsPtr &obj, Formatter &fmt): obj(obj), fmt(fmt) {}
        void print(std::ostream &stream) const { obj->print(stream, fmt); }
    };

    /** Used for printing RISC operators with formatting. The usual way to use this is:
     * @code
     *  RiscOperatorsPtr obj = ...;
     *  Formatter fmt = ...;
     *  std::cout <<"The value is: " <<(*obj+fmt) <<"\n";
     * @endcode
     * @{ */
    WithFormatter with_format(Formatter &fmt) { return WithFormatter(shared_from_this(), fmt); }
    WithFormatter operator+(Formatter &fmt) { return with_format(fmt); }
    /** @} */

    /** Property: Number of instructions processed.
     *
     *  This counter is incremented at the beginning of each instruction.
     *
     * @{ */
    virtual size_t nInsns() const { return nInsns_; }
    virtual void nInsns(size_t n) { nInsns_ = n; }
    /** @} */

    /** Returns current instruction.
     *
     *  Returns the instruction which is being processed. This is set by @ref startInstruction and cleared by @ref
     *  finishInstruction. Returns null if we are not processing an instruction. */
    virtual SgAsmInstruction* currentInstruction() const {
        return currentInsn_;
    }

    /** Called at the beginning of every instruction.  This method is invoked every time the translation object begins
     *  processing an instruction.  Some policies use this to update a pointer to the current instruction. */
    virtual void startInstruction(SgAsmInstruction *insn);

    /** Called at the end of every instruction.  This method is invoked whenever the translation object ends processing for an
     *  instruction.  This is not called if there's an exception during processing. */
    virtual void finishInstruction(SgAsmInstruction *insn) {
        ASSERT_not_null(insn);
        ASSERT_require(currentInsn_==insn);
        currentInsn_ = NULL;
    };


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                  Value Construction Operations
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // The trailing underscores are necessary for for undefined_() on some machines, so we just add one to the end of all the
    // virtual constructors for consistency.

    /** Returns a new undefined value. Uses the prototypical value to virtually construct the new value. */
    virtual SValuePtr undefined_(size_t nbits) {
        return protoval_->undefined_(nbits);
    }
    virtual SValuePtr unspecified_(size_t nbits) {
        return protoval_->unspecified_(nbits);
    }

    /** Returns a number of the specified bit width.  Uses the prototypical value to virtually construct a new value. */
    virtual SValuePtr number_(size_t nbits, uint64_t value) {
        return protoval_->number_(nbits, value);
    }

    /** Returns a Boolean value. Uses the prototypical value to virtually construct a new value. */
    virtual SValuePtr boolean_(bool value) {
        return protoval_->boolean_(value);
    }

    /** Returns a data-flow bottom value. Uses the prototypical value to virtually construct a new value. */
    virtual SValuePtr bottom_(size_t nbits) {
        return protoval_->bottom_(nbits);
    }

    
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                  x86-specific Operations (FIXME)
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    /** Invoked to filter call targets.  This method is called whenever the translation object is about to invoke a function
     *  call.  The target address is passed as an argument and a (new) target should be returned. */
    virtual SValuePtr filterCallTarget(const SValuePtr &a) {
        return a->copy();
    }

    /** Invoked to filter return targets.  This method is called whenever the translation object is about to return from a
     *  function call (such as for the x86 "RET" instruction).  The return address is passed as an argument and a (new) return
     *  address should be returned. */
    virtual SValuePtr filterReturnTarget(const SValuePtr &a) {
        return a->copy();
    }

    /** Invoked to filter indirect jumps.  This method is called whenever the translation object is about to unconditionally
     *  jump to a new address (such as for the x86 "JMP" instruction).  The target address is passed as an argument and a (new)
     *  target address should be returned. */
    virtual SValuePtr filterIndirectJumpTarget(const SValuePtr &a) {
        return a->copy();
    }

    /** Invoked for the x86 HLT instruction. */
    virtual void hlt() {}

    /** Invoked for the x86 CPUID instruction. FIXME: x86-specific stuff should be in the dispatcher. */
    virtual void cpuid() {}

    /** Invoked for the x86 RDTSC instruction. FIXME: x86-specific stuff should be in the dispatcher. */
    virtual SValuePtr rdtsc() { return unspecified_(64); }


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                  Boolean Operations
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    /** Computes bit-wise AND of two values. The operands must both have the same width; the result must be the same width as
     *  the operands. */
    virtual SValuePtr and_(const SValuePtr &a, const SValuePtr &b) = 0;

    /** Computes bit-wise OR of two values. The operands @p a and @p b must have the same width; the return value width will
     * be the same as @p a and @p b. */
    virtual SValuePtr or_(const SValuePtr &a, const SValuePtr &b) = 0;

    /** Computes bit-wise XOR of two values. The operands @p a and @p b must have the same width; the result will be the same
     *  width as @p a and @p b. */
    virtual SValuePtr xor_(const SValuePtr &a, const SValuePtr &b) = 0;

    /** One's complement. The result will be the same size as the operand. */
    virtual SValuePtr invert(const SValuePtr &a) = 0;

    /** Extracts bits from a value.  The specified bits from begin_bit (inclusive) through end_bit (exclusive) are copied into
     *  the low-order bits of the return value (other bits in the return value are cleared). The least significant bit is
     *  number zero. The begin_bit and end_bit values must be valid for the width of @p a. */
    virtual SValuePtr extract(const SValuePtr &a, size_t begin_bit, size_t end_bit) = 0;

    /** Concatenates the bits of two values.  The bits of @p a and @p b are concatenated so that the result has @p
     *  b in the high-order bits and @p a in the low order bits. The width of the return value is the sum of the widths of @p
     *  a and @p b. */
    virtual SValuePtr concat(const SValuePtr &a, const SValuePtr &b) = 0;

    /** Returns position of least significant set bit; zero when no bits are set. The return value will have the same width as
     * the operand, although this can be safely truncated to the log-base-2 + 1 width. */
    virtual SValuePtr leastSignificantSetBit(const SValuePtr &a) = 0;

    /** Returns position of most significant set bit; zero when no bits are set. The return value will have the same width as
     * the operand, although this can be safely truncated to the log-base-2 + 1 width. */
    virtual SValuePtr mostSignificantSetBit(const SValuePtr &a) = 0;

    /** Rotate bits to the left. The return value will have the same width as operand @p a.  The @p nbits is interpreted as
     *  unsigned. The behavior is modulo the width of @p a regardles of whether the implementation makes that a special case or
     *  handles it naturally. */
    virtual SValuePtr rotateLeft(const SValuePtr &a, const SValuePtr &nbits) = 0;

    /** Rotate bits to the right. The return value will have the same width as operand @p a.  The @p nbits is interpreted as
     *  unsigned. The behavior is modulo the width of @p a regardles of whether the implementation makes that a special case or
     *  handles it naturally. */
    virtual SValuePtr rotateRight(const SValuePtr &a, const SValuePtr &nbits) = 0;

    /** Returns arg shifted left. The return value will have the same width as operand @p a.  The @p nbits is interpreted as
     *  unsigned. New bits shifted into the value are zero. If @p nbits is equal to or larger than the width of @p a then the
     *  result is zero. */
    virtual SValuePtr shiftLeft(const SValuePtr &a, const SValuePtr &nbits) = 0;

    /** Returns arg shifted right logically (no sign bit). The return value will have the same width as operand @p a. The @p
     *  nbits is interpreted as unsigned. New bits shifted into the value are zero. If  @p nbits is equal to or larger than the
     *  width of @p a then the result is zero. */
    virtual SValuePtr shiftRight(const SValuePtr &a, const SValuePtr &nbits) = 0;

    /** Returns arg shifted right arithmetically (with sign bit). The return value will have the same width as operand @p
     *  a. The @p nbits is interpreted as unsigned. New bits shifted into the value are the same as the most significant bit
     *  (the "sign bit"). If @p nbits is equal to or larger than the width of @p a then the result has all bits cleared or all
     *  bits set depending on whether the most significant bit was originally clear or set. */
    virtual SValuePtr shiftRightArithmetic(const SValuePtr &a, const SValuePtr &nbits) = 0;


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                  Comparison Operations
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    
    /** Determines whether a value is equal to zero.  Returns true, false, or undefined (in the semantic domain) depending on
     *  whether argument is zero. */
    virtual SValuePtr equalToZero(const SValuePtr &a) = 0;

    /** If-then-else.  Returns operand @p a if @p cond is true, operand @p b if @p cond is false, or some other value if the
     *  condition is unknown. The @p condition must be one bit wide; the widths of @p a and @p b must be equal; the return
     *  value width will be the same as @p a and @p b. */
    virtual SValuePtr ite(const SValuePtr &cond, const SValuePtr &a, const SValuePtr &b) = 0;

    /** Equality comparison.
     *
     *  Returns a Boolean to indicate whether the relationship between @p a and @p b holds. Both operands must be the same
     *  width. It doesn't matter if they are interpreted as signed or unsigned quantities.
     *
     * @{ */
    virtual SValuePtr isEqual(const SValuePtr &a, const SValuePtr &b);
    virtual SValuePtr isNotEqual(const SValuePtr &a, const SValuePtr &b);
    /** @} */

    /** Comparison for unsigned values.
     *
     *  Returns a Boolean to indicate whether the relationship between @p a and @p b is true when @p a and @p b are interpreted
     *  as unsigned values.  Both values must have the same width.  This operation is a convenience wrapper around other RISC
     *  operators.
     *
     * @{ */
    virtual SValuePtr isUnsignedLessThan(const SValuePtr &a, const SValuePtr &b);
    virtual SValuePtr isUnsignedLessThanOrEqual(const SValuePtr &a, const SValuePtr &b);
    virtual SValuePtr isUnsignedGreaterThan(const SValuePtr &a, const SValuePtr &b);
    virtual SValuePtr isUnsignedGreaterThanOrEqual(const SValuePtr &a, const SValuePtr &b);
    /** @} */

    /** Comparison for signed values.
     *
     *  Returns a Boolean to indicate whether the relationship between @p a and @p b is true when @p a and @p b are interpreted
     *  as signed values.  Both values must have the same width.  This operation is a convenience wrapper around other RISC
     *  operators.
     *
     * @{ */
    virtual SValuePtr isSignedLessThan(const SValuePtr &a, const SValuePtr &b);
    virtual SValuePtr isSignedLessThanOrEqual(const SValuePtr &a, const SValuePtr &b);
    virtual SValuePtr isSignedGreaterThan(const SValuePtr &a, const SValuePtr &b);
    virtual SValuePtr isSignedGreaterThanOrEqual(const SValuePtr &a, const SValuePtr &b);
    /** @} */

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                  Integer Arithmetic Operations
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    /** Extend (or shrink) operand @p a so it is @p nbits wide by adding or removing high-order bits. Added bits are always
     *  zeros. The result will be the specified @p new_width. */
    virtual SValuePtr unsignedExtend(const SValuePtr &a, size_t new_width) {
        return a->copy(new_width);
    }

    /** Sign extends a value. The result will the the specified @p new_width, which must be at least as large as the original
     * width. */
    virtual SValuePtr signExtend(const SValuePtr &a, size_t new_width) = 0;

    /** Adds two integers of equal size.  The width of @p a and @p b must be equal; the return value will have the same width
     * as @p a and @p b. */
    virtual SValuePtr add(const SValuePtr &a, const SValuePtr &b) = 0;

    /** Subtract one value from another.  This is not a virtual function because it can be implemented in terms of @ref add and
     * @ref negate. We define it because it's something that occurs often enough to warrant its own function. */
    virtual SValuePtr subtract(const SValuePtr &minuend, const SValuePtr &subtrahend);

    /** Add two values of equal size and a carry bit.  Carry information is returned via carry_out argument.  The carry_out
     *  value is the tick marks that are written above the first addend when doing long arithmetic like a 2nd grader would do
     *  (of course, they'd probably be adding two base-10 numbers).  For instance, when adding 00110110 and 11100100:
     *
     *  \code
     *    '''..'..         <-- carry tick marks: '=carry .=no carry
     *     00110110
     *   + 11100100
     *   ----------
     *    100011010
     *  \endcode
     *
     *  The carry_out value is 11100100.
     *
     *  The width of @p a and @p b must be equal; @p c must have a width of one bit; the return value and @p carry_out will be
     *  the same width as @p a and @p b.  The @p carry_out value is allocated herein. */
    virtual SValuePtr addWithCarries(const SValuePtr &a, const SValuePtr &b, const SValuePtr &c,
                                        SValuePtr &carry_out/*output*/) = 0;

    /** Two's complement. The return value will have the same width as the operand. */
    virtual SValuePtr negate(const SValuePtr &a) = 0;

    /** Divides two signed values. The width of the result will be the same as the width of operand @p a. */
    virtual SValuePtr signedDivide(const SValuePtr &a, const SValuePtr &b) = 0;

    /** Calculates modulo with signed values. The width of the result will be the same as the width of operand @p b. */
    virtual SValuePtr signedModulo(const SValuePtr &a, const SValuePtr &b) = 0;

    /** Multiplies two signed values. The width of the result will be the sum of the widths of @p a and @p b. */
    virtual SValuePtr signedMultiply(const SValuePtr &a, const SValuePtr &b) = 0;

    /** Divides two unsigned values. The width of the result is the same as the width of operand @p a. */
    virtual SValuePtr unsignedDivide(const SValuePtr &a, const SValuePtr &b) = 0;

    /** Calculates modulo with unsigned values. The width of the result is the same as the width of operand @p b. */
    virtual SValuePtr unsignedModulo(const SValuePtr &a, const SValuePtr &b) = 0;

    /** Multiply two unsigned values. The width of the result is the sum of the widths of @p a and @p b. */
    virtual SValuePtr unsignedMultiply(const SValuePtr &a, const SValuePtr &b) = 0;


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                  Interrupt and system calls
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    /** Invoked for instructions that cause an interrupt.  The major and minor numbers are architecture specific.  For
     *  instance, an x86 INT instruction uses major number zero and the minor number is the interrupt number (e.g., 0x80 for
     *  Linux system calls), while an x86 SYSENTER instruction uses major number one. The minr operand for INT3 is -3 to
     *  distinguish it from the one-argument "INT 3" instruction which has slightly different semantics. */
    virtual void interrupt(int /*majr*/, int /*minr*/) {}


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                  Floating-point operations
    //
    // For now these all have default implementations that throw NotImplemented, but we might change them to pure virtual
    // sometime in the future so they're consistent with most other RISC operators. [Robb P. Matzke 2015-08-03]
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    /** Construct a floating-point value from an integer value. */
    virtual SValuePtr fpFromInteger(const SValuePtr &intValue, SgAsmFloatType *fpType);

    /** Construct an integer value from a floating-point value.
     *
     *  The bits of @p fpValue are interpreted according to the @p fpType and converted to a signed integer value
     *  that fits in @p integerWidth bits. This is done by truncating the fractional part of the floating point number, thus
     *  rounding toward zero. If @p fpValue is not a number then @p dflt is returned. */
    virtual SValuePtr fpToInteger(const SValuePtr &fpValue, SgAsmFloatType *fpType, const SValuePtr &dflt);

    /** Convert from one floating-point type to another.
     *
     *  Converts the floating-point value @p a having type @p aType to the return value having @p retType. */
    virtual SValuePtr fpConvert(const SValuePtr &a, SgAsmFloatType *aType, SgAsmFloatType *retType);

    /** Whether a floating-point value is a special not-a-number bit pattern. */
    virtual SValuePtr fpIsNan(const SValuePtr &fpValue, SgAsmFloatType *fpType);

    /** Whether a floating-point value is denormalized. */
    virtual SValuePtr fpIsDenormalized(const SValuePtr &fpValue, SgAsmFloatType *fpType);

    /** Whether a floating-point value is equal to zero. */
    virtual SValuePtr fpIsZero(const SValuePtr &fpValue, SgAsmFloatType *fpType);

    /** Whether a floating-point value is infinity.
     *
     *  Returns true if the floating point value is plus or minus infinity.  Querying the sign bit will return the sign of the
     *  infinity. */
    virtual SValuePtr fpIsInfinity(const SValuePtr &fpValue, SgAsmFloatType *fpType);

    /** Sign of floating-point value.
     *
     *  Returns the value of the floating-point sign bit. */
    virtual SValuePtr fpSign(const SValuePtr &fpValue, SgAsmFloatType *fpType);

    /** Exponent of floating-point value.
     *
     *  Returns the exponent of the floating point value. For normalized values this returns the stored exponent minus the
     *  exponent bias.  For denormalized numbers this returns the stored exponent minus the exponent bias minus an additional
     *  amount to normalize the significand. */
    virtual SValuePtr fpEffectiveExponent(const SValuePtr &fpValue, SgAsmFloatType *fpType);

    /** Add two floating-point values.
     *
     *  Adds two floating-point values that have the same type and returns the sum in the same type. */
    virtual SValuePtr fpAdd(const SValuePtr &a, const SValuePtr &b, SgAsmFloatType *fpType);

    /** Subtract one floating-point value from another.
     *
     *  Subtracts @p b from @p a and returns the difference. All three floating-point values have the same type.  The default
     *  implementation is in terms of negate and add. */
    virtual SValuePtr fpSubtract(const SValuePtr &a, const SValuePtr &b, SgAsmFloatType *fpType);

    /** Multiply two floating-point values.
     *
     *  Multiplies two floating-point values and returns the product. All three values have the same type. */
    virtual SValuePtr fpMultiply(const SValuePtr &a, const SValuePtr &b, SgAsmFloatType *fpType);

    /** Divide one floating-point value by another.
     *
     *  Computes @p a divided by @p b and returns the result. All three floating-point values have the same type. */
    virtual SValuePtr fpDivide(const SValuePtr &a, const SValuePtr &b, SgAsmFloatType *fpType);

    /** Square root.
     *
     *  Computes and returns the square root of the specified floating-point value.  Both values have the same type. */
    virtual SValuePtr fpSquareRoot(const SValuePtr &a, SgAsmFloatType *fpType);

    /** Round toward zero.
     *
     *  Truncate the fractional part of the floating point number. */
    virtual SValuePtr fpRoundTowardZero(const SValuePtr &a, SgAsmFloatType *fpType);

    
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                  State Accessing Operations
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    
    /** Reads a value from a register.
     *
     *  The base implementation simply delegates to the current semantic State, which probably delegates to a register state,
     *  but subclasses are welcome to override this behavior at any level.
     *
     *  A register state will typically implement storage for hardware registers, but higher layers (the State, RiscOperators,
     *  Dispatcher, ...)  should not be concerned about the size of the register they're trying to read.  For example, a
     *  register state for a 32-bit x86 architecture will likely have a storage location for the 32-bit EAX register, but it
     *  should be possible to ask @ref readRegister to return the value of AX (the low-order 16-bits).  In order to accomplish
     *  this, some level of the readRegister delegations needs to invoke @ref extract to obtain the low 16 bits.  The
     *  RiscOperators object is passed along the delegation path for this purpose.  The inverse @ref concat operation will be
     *  needed at some level when we ask @ref readRegister to return a value that comes from multiple storage locations in the
     *  register state (such as can happen if an x86 register state holds individual status flags and we ask for the 32-bit
     *  EFLAGS register).
     *
     *  If the register state can distinguish between a register that has never been accessed and a register that has only been
     *  read, then the @p dflt value is stored into the register the first time it's read. This ensures that reading the
     *  register a second time with no intervening write will return the same value as the first read.  If a @p dflt is not
     *  provided then one is constructed by invoking @ref undefined_.
     *
     *  There needs to be a certain level of cooperation between the RiscOperators, State, and register state classes to decide
     *  which layer should invoke the @ref extract or @ref concat (or whatever other RISC operations might be necessary).
     *
     *  @{ */
    virtual SValuePtr readRegister(RegisterDescriptor reg) {   // old subclasses can still override this if they want,
        return readRegister(reg, undefined_(reg.get_nbits())); // but new subclasses should not override this method.
    }
    virtual SValuePtr readRegister(RegisterDescriptor reg, const SValuePtr &dflt); // new subclasses override this
    /** @} */

    /** Writes a value to a register.
     *
     *  The base implementation simply delegates to the current semantic State, which probably delegates to a register state,
     *  but subclasses are welcome to override this behavior at any level.
     *
     *  As with @ref readRegister, @ref writeRegister may need to perform various RISC operations in order to accomplish the
     *  task of writing a value to the specified register when the underlying register state doesn't actually store a value for
     *  that specific register. The RiscOperations object is passed along for that purpose.  See @ref readRegister for more
     *  details. */
    virtual void writeRegister(RegisterDescriptor reg, const SValuePtr &a) {
        ASSERT_not_null(currentState_);
        currentState_->writeRegister(reg, a, this);
    }

    /** Obtain a register value without side effects.
     *
     *  This is a lower-level operation than @ref readRegister in that it doesn't cause the register to be marked as having
     *  been read. It is typically used in situations where the register is being accessed for analysis purposes rather than as
     *  part of an instruction emulation.
     *
     * @{ */
    virtual SValuePtr peekRegister(RegisterDescriptor, const SValuePtr &dflt);
    SValuePtr peekRegister(RegisterDescriptor reg) {
        return peekRegister(reg, undefined_(reg.get_nbits()));
    }
    /** @} */

    /** Reads a value from memory.
     *
     *  The implementation (in subclasses) will typically delegate much of the work to the current state's @ref
     *  State::readMemory "readMemory" method.
     *
     *  A MemoryState will implement storage for memory locations and might impose certain restrictions, such as "all memory
     *  values must be eight bits".  However, the @ref readMemory should not have these constraints so that it can
     *  be called from a variety of Dispatcher subclass (e.g., the DispatcherX86 class assumes that @ref readMemory
     *  is capable of reading 32-bit values from little-endian memory). The designers of the MemoryState, State, and
     *  RiscOperators should collaborate to decide which layer (RiscOperators, State, or MemoryState) is reponsible for
     *  combining individual memory locations into larger values.  A RiscOperators object is passed along the chain of
     *  delegations for this purpose. The RiscOperators might also contain other data that's import during the process, such as
     *  an SMT solver.
     *
     *  The @p segreg argument is an optional segment register. Most architectures have a flat virtual address space and will
     *  pass a default-constructed register descriptor whose is_valid() method returns false.
     *
     *  The @p cond argument is a Boolean value that indicates whether this is a true read operation. If @p cond can be proven
     *  to be false then the read is a no-op and returns an arbitrary value.
     *
     *  The @p dflt argument determines the size of the value to be read. This argument is also passed along to the lower
     *  layers so that they can, if they desire, use it to initialize memory that has never been read or written before. */
    virtual SValuePtr readMemory(RegisterDescriptor segreg, const SValuePtr &addr, const SValuePtr &dflt,
                                 const SValuePtr &cond) = 0;

    /** Writes a value to memory.
     *
     *  The implementation (in subclasses) will typically delegate much of the work to the current state's @ref
     *  State::writeMemory "writeMemory" method.
     *
     *  The @p segreg argument is an optional segment register. Most architectures have a flat virtual address space and will
     *  pass a default-constructed register descriptor whose is_valid() method returns false.
     *
     *  The @p cond argument is a Boolean value that indicates whether this is a true write operation. If @p cond can be proved
     *  to be false then writeMemory is a no-op. */
    virtual void writeMemory(RegisterDescriptor segreg, const SValuePtr &addr, const SValuePtr &data,
                             const SValuePtr &cond) = 0;

    /** Read memory without side effects.
     *
     *  This is a lower-level operation than @ref readMemory in that it doesn't cause any side effects in the memory state. In
     *  all other respects, it's similar to @ref readMemory. */
    virtual SValuePtr peekMemory(RegisterDescriptor segreg, const SValuePtr &addr, const SValuePtr &dflt) = 0;
};



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                      Instruction Dispatcher
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Shared-ownership pointer to a semantics instruction dispatcher. See @ref heap_object_shared_ownership. */
typedef boost::shared_ptr<class Dispatcher> DispatcherPtr;

/** Functor that knows how to dispatch a single kind of instruction. */
class InsnProcessor {
public:
    virtual ~InsnProcessor() {}
    virtual void process(const DispatcherPtr &dispatcher, SgAsmInstruction *insn) = 0;
};
    
/** Dispatches instructions through the RISC layer.
 *
 *  The dispatcher is the instruction semantics entity that translates a high-level architecture-dependent instruction into a
 *  sequence of RISC operators whose interface is defined by ROSE. These classes are the key in ROSE's ability to connect a
 *  variety of instruction set architectures to a variety of semantic domains.
 *
 *  Each dispatcher contains a table indexed by the machine instruction "kind" (e.g., SgAsmMipsInstruction::get_kind()). The
 *  table stores functors derived from the abstract InsnProcessor class.  (FIXME: The functors are not currently reference
 *  counted; they are owned by the dispatcher and deleted when the dispatcher is destroyed. [Robb Matzke 2013-03-04])
 *
 *  Dispatcher objects are allocated on the heap and reference counted.  The BaseSemantics::Dispatcher is an abstract class
 *  that defines the interface.  See the Rose::BinaryAnalysis::InstructionSemantics2 namespace for an overview of how the parts
 *  fit together. */
class Dispatcher: public boost::enable_shared_from_this<Dispatcher> {
protected:
    RiscOperatorsPtr operators;
    const RegisterDictionary *regdict;                  /**< See set_register_dictionary(). */
    size_t addrWidth_;                                  /**< Width of memory addresses in bits. */
    bool autoResetInstructionPointer_;                  /**< Reset instruction pointer register for each instruction. */

    // Dispatchers keep a table of all the kinds of instructions they can handle.  The lookup key is typically some sort of
    // instruction identifier, such as from SgAsmX86Instruction::get_kind(), and comes from the iproc_key() virtual method.
    typedef std::vector<InsnProcessor*> InsnProcessors;
    InsnProcessors iproc_table;

#ifdef ROSE_HAVE_BOOST_SERIALIZATION_LIB
private:
    friend class boost::serialization::access;

    template<class S>
    void serialize(S &s, const unsigned /*version*/) {
        s & BOOST_SERIALIZATION_NVP(operators);
        s & BOOST_SERIALIZATION_NVP(regdict);
        s & BOOST_SERIALIZATION_NVP(addrWidth_);
        s & BOOST_SERIALIZATION_NVP(autoResetInstructionPointer_);
        //s & iproc_table; -- not saved
    }
#endif
    
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Real constructors
protected:
    // Prototypical constructor
    Dispatcher(): regdict(NULL), addrWidth_(0), autoResetInstructionPointer_(true) {}

    // Prototypical constructor
    Dispatcher(size_t addrWidth, const RegisterDictionary *regs)
        : regdict(regs), addrWidth_(addrWidth), autoResetInstructionPointer_(true) {}

    Dispatcher(const RiscOperatorsPtr &ops, size_t addrWidth, const RegisterDictionary *regs)
        : operators(ops), regdict(regs), addrWidth_(addrWidth), autoResetInstructionPointer_(true) {
        ASSERT_not_null(operators);
        ASSERT_not_null(regs);
    }

public:
    /** Shared-ownership pointer for a @ref Dispatcher object. See @ref heap_object_shared_ownership. */
    typedef DispatcherPtr Ptr;

public:
    virtual ~Dispatcher() {
        for (InsnProcessors::iterator iter=iproc_table.begin(); iter!=iproc_table.end(); ++iter)
            delete *iter;
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Static allocating constructors. None since this is an abstract class


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Virtual constructors
public:
    /** Virtual constructor. */
    virtual DispatcherPtr create(const RiscOperatorsPtr &ops, size_t addrWidth=0, const RegisterDictionary *regs=NULL) const = 0;

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Methods to process instructions
public:
    /** Process a single instruction. */
    virtual void processInstruction(SgAsmInstruction *insn);

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Instruction processor table operations
public:
    /** Lookup the processor for an instruction.  Looks up the functor that has been registered to process the given
     *  instruction. Returns the null pointer if the instruction cannot be processed. Instruction processor objects are
     *  managed by the caller; the instruction itself is only used for the duration of this call. */
    virtual InsnProcessor *iproc_lookup(SgAsmInstruction *insn);

    /** Replace an instruction processor with another.  The processor for the specified instruction is replaced with the
     *  specified processor, which may be the null pointer.  Instruction processor objects are managed by the caller; the
     *  instruction itself is only used for the duration of this call. */
    virtual void iproc_replace(SgAsmInstruction *insn, InsnProcessor *iproc);    

    /** Given an instruction, return the InsnProcessor key that can be used as an index into the iproc_table. */
    virtual int iproc_key(SgAsmInstruction*) const = 0;

    /** Set an iproc table entry to the specified value.
     *
     *  The @p iproc object will become owned by this dispatcher and deleted when this dispatcher is destroyed. */
    virtual void iproc_set(int key, InsnProcessor *iproc);

    /** Obtain an iproc table entry for the specified key. */
    virtual InsnProcessor *iproc_get(int key);

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Convenience methods that defer the call to some member object
public:
    /** Get a pointer to the RISC operators object. */
    virtual RiscOperatorsPtr get_operators() const { return operators; }

    /** Get a pointer to the state object. The state is stored in the RISC operators object, so this is just here for
     *  convenience. */
    virtual StatePtr currentState() const { return operators ? operators->currentState() : StatePtr(); }

    /** Return the prototypical value.  The prototypical value comes from the RISC operators object. */
    virtual SValuePtr protoval() const { return operators ? operators->protoval() : SValuePtr(); }

    /** Returns the instruction that is being processed.
     *
     *  The instruction comes from the @ref RiscOperators::currentInstruction "currentInstruction" method of the RiscOperators
     *  object. */
    virtual SgAsmInstruction* currentInstruction() const {
         return operators ? operators->currentInstruction() : NULL;
    }

    /** Return a new undefined semantic value. */
    virtual SValuePtr undefined_(size_t nbits) const {
        ASSERT_not_null(operators);
        return operators->undefined_(nbits);
    }
    virtual SValuePtr unspecified_(size_t nbits) const {
        ASSERT_not_null(operators);
        return operators->unspecified_(nbits);
    }

    /** Return a semantic value representing a number. */
    virtual SValuePtr number_(size_t nbits, uint64_t number) const {
        ASSERT_not_null(operators);
        return operators->number_(nbits, number);
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Methods related to registers
public:
    /** Access the register dictionary.  The register dictionary defines the set of registers over which the RISC operators may
     *  operate. This should be same registers (or superset thereof) whose values are stored in the machine state(s).  This
     *  dictionary is used by the Dispatcher class to translate register names to register descriptors.  For instance, to read
     *  from the "eax" register, the dispatcher will look up "eax" in its register dictionary and then pass that descriptor to
     *  the @ref RiscOperators::readRegister operation.  Register descriptors are also stored in instructions when the
     *  instruction is disassembled, so the dispatcher should probably be using the same registers as the disassembler, or a
     *  superset thereof.
     *
     *  The register dictionary should not be changed after a dispatcher is instantiated because the dispatcher's constructor
     *  may query the dictionary and cache the resultant register descriptors.
     * @{ */
    virtual const RegisterDictionary *get_register_dictionary() const {
        return regdict;
    }
    virtual void set_register_dictionary(const RegisterDictionary *regdict) {
        this->regdict = regdict;
    }
    /** @} */

    /** Lookup a register by name.  This dispatcher's register dictionary is consulted and the specified register is located by
     *  name.  If a bit width is specified (@p nbits) then it must match the size of register that was found.  If a valid
     *  register cannot be found then either an exception is thrown or an invalid register is returned depending on whether
     *  @p allowMissing is false or true, respectively. */
    virtual RegisterDescriptor findRegister(const std::string &regname, size_t nbits=0, bool allowMissing=false) const;

    /** Property: Width of memory addresses.
     *
     *  This property defines the width of memory addresses. All memory reads and writes (and any other defined memory
     *  operations) should pass address expressions that are this width.  The address width cannot be changed once it's set.
     *
     * @{ */
    size_t addressWidth() const { return addrWidth_; }
    void addressWidth(size_t nbits);
    /** @} */

    /** Returns the instruction pointer register. */
    virtual RegisterDescriptor instructionPointerRegister() const = 0;

    /** Returns the stack pointer register. */
    virtual RegisterDescriptor stackPointerRegister() const = 0;

    /** Property: Reset instruction pointer register for each instruction.
     *
     *  If this property is set, then each time an instruction is processed, the first thing that happens is that the
     *  instruction pointer register is reset to the concrete address of the instruction.
     *
     * @{ */
    bool autoResetInstructionPointer() const { return autoResetInstructionPointer_; }
    void autoResetInstructionPointer(bool b) { autoResetInstructionPointer_ = b; }
    /** @} */
    
    
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // Miscellaneous methods that tend to be the same for most dispatchers
public:

    /** Update the instruction pointer register.
     *
     *  Causes the instruction pointer register to point to the address following the specified instruction.  Since every
     *  instruction has a concrete address, we could simply set the instruction pointer to that concrete address. However, some
     *  analyses depend on having an instruction pointer value that's built up by processing one instruction after
     *  another.  Therefore, if we can recognize the register state implementation and determine that the instruction pointer
     *  registers' value is already stored, we'll increment that value, which might result in a concrete value depending on the
     *  semantic domain. Otherwise we just explicitly assign a new concrete value to that register. */
    virtual void advanceInstructionPointer(SgAsmInstruction*);

    /** Returns a register descriptor for the segment part of a memory reference expression.  Many architectures don't use
     *  segment registers (they have a flat virtual address space), in which case the returned register descriptor's is_valid()
     *  method returns false. */
    virtual RegisterDescriptor segmentRegister(SgAsmMemoryReferenceExpression*);

    /** Increment all auto-increment registers in the expression.  This method traverses the expression and increments each
     *  the register of each register reference expression that has a positive adjustment value.  If the same register is
     *  encountered multiple times then it is incremented multiple times. */
    virtual void incrementRegisters(SgAsmExpression*);

    /** Decrement all auto-decrement registers in the expression.  This method traverses the expression and increments each
     *  the register of each register reference expression that has a negative adjustment value.  If the same register is
     *  encountered multiple times then it is decremented multiple times. */
    virtual void decrementRegisters(SgAsmExpression*);

    /** Returns a memory address by evaluating the address expression.  The address expression can be either a constant or an
     *  expression containing operators and constants.  If @p nbits is non-zero then the result is sign extended or truncated
     *  to the specified width, otherwise the returned SValue is the natural width of the expression. */
    virtual SValuePtr effectiveAddress(SgAsmExpression*, size_t nbits=0);

    /** Reads an R-value expression.  The expression can be a constant, register reference, or memory reference.  The width of
     *  the returned value is specified by the @p value_nbits argument, and if this argument is zero then the width of the
     *  expression type is used.  The width of the address passed to lower-level memory access functions is specified by @p
     *  addr_nbits.  If @p addr_nbits is zero then the natural width of the effective address is passed to lower level
     *  functions. */
    virtual SValuePtr read(SgAsmExpression*, size_t value_nbits=0, size_t addr_nbits=0);

    /** Writes to an L-value expression. The expression can be a register or memory reference.  The width of the address passed
     *  to lower-level memory access functions is specified by @p addr_nbits.  If @p addr_nbits is zero then the natural width
     *  of the effective address is passed to lower level functions. */
    virtual void write(SgAsmExpression*, const SValuePtr &value, size_t addr_nbits=0);
};



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                      Printing
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

std::ostream& operator<<(std::ostream&, const Exception&);
std::ostream& operator<<(std::ostream&, const SValue&);
std::ostream& operator<<(std::ostream&, const SValue::WithFormatter&);
std::ostream& operator<<(std::ostream&, const MemoryState&);
std::ostream& operator<<(std::ostream&, const MemoryState::WithFormatter&);
std::ostream& operator<<(std::ostream&, const RegisterState&);
std::ostream& operator<<(std::ostream&, const RegisterState::WithFormatter&);
std::ostream& operator<<(std::ostream&, const State&);
std::ostream& operator<<(std::ostream&, const State::WithFormatter&);
std::ostream& operator<<(std::ostream&, const RiscOperators&);
std::ostream& operator<<(std::ostream&, const RiscOperators::WithFormatter&);

} // namespace
} // namespace
} // namespace
} // namespace

// Class versions must be at global scope
BOOST_CLASS_VERSION(Rose::BinaryAnalysis::InstructionSemantics2::BaseSemantics::RegisterState, 1);

#endif
