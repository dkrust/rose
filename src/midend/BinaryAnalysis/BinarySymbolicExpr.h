#ifndef ROSE_BinaryAnalysis_SymbolicExpr_H
#define ROSE_BinaryAnalysis_SymbolicExpr_H

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include "Map.h"

#include <boost/any.hpp>
#include <boost/logic/tribool.hpp>
#include <boost/serialization/access.hpp>
#include <boost/serialization/base_object.hpp>
#include <boost/serialization/export.hpp>
#include <boost/serialization/split_member.hpp>
#include <boost/serialization/string.hpp>
#include <boost/serialization/vector.hpp>
#include <boost/unordered_map.hpp>
#include <cassert>
#include <inttypes.h>
#include <RoseException.h>
#include <Sawyer/Attribute.h>
#include <Sawyer/BitVector.h>
#include <Sawyer/Set.h>
#include <Sawyer/SharedPointer.h>
#include <Sawyer/SmallObject.h>
#include <set>

namespace Rose {
namespace BinaryAnalysis {

class SmtSolver;
typedef Sawyer::SharedPointer<SmtSolver> SmtSolverPtr;

/** Namespace supplying types and functions for symbolic expressions.
 *
 *  These are used by certain instruction semantics policies and satisfiability modulo theory (SMT) solvers. These expressions
 *  are tailored to bit-vector and integer difference logics, whereas the expression nodes in other parts of ROSE have
 *  different goals. */
namespace SymbolicExpr {

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                      Basic Types
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Exceptions for symbolic expressions. */
class Exception: public Rose::Exception {
public:
    explicit Exception(const std::string &mesg): Rose::Exception(mesg) {}
};

/** Operators for interior nodes of the expression tree.
 *
 *  Commutative operators generally take one or more operands.  Operators such as shifting, extending, and truncating have the
 *  size operand appearing before the bit vector on which to operate (this makes the output more human-readable since the size
 *  operand is often a constant). */
enum Operator {
    OP_ADD,                 /**< Addition. One or more operands, all the same width. */
    OP_AND,                 /**< Bitwise conjunction. One or more operands all the same width. */
    OP_ASR,                 /**< Arithmetic shift right. Operand B shifted by A bits; 0 <= A < width(B). A is unsigned. */
    OP_CONCAT,              /**< Concatenation. Operand A becomes high-order bits. Any number of operands. */
    OP_EQ,                  /**< Equality. Two operands, both the same width. */
    OP_EXTRACT,             /**< Extract subsequence of bits. Extract bits [A..B) of C. 0 <= A < B <= width(C). */
    OP_INVERT,              /**< Bitwise inversion. One operand. */
    OP_ITE,                 /**< If-then-else. A must be one bit. Returns B if A is set, C otherwise. */
    OP_LET,                 /**< Let expression. Deferred substitution. Substitutes A for B in C. */
    OP_LSSB,                /**< Least significant set bit or zero. One operand. */
    OP_MSSB,                /**< Most significant set bit or zero. One operand. */
    OP_NE,                  /**< Inequality. Two operands, both the same width. */
    OP_NEGATE,              /**< Arithmetic negation. One operand. For Booleans, use OP_INVERT (2's complement is a no-op). */
    OP_NOOP,                /**< No operation. Used only by the default constructor. */
    OP_OR,                  /**< Bitwise disjunction. One or more operands all the same width. */
    OP_READ,                /**< Read a value from memory.  Arguments are the memory state and the address expression. */
    OP_ROL,                 /**< Rotate left. Rotate bits of B left by A bits.  0 <= A < width(B). A is unsigned. */
    OP_ROR,                 /**< Rotate right. Rotate bits of B right by A bits. 0 <= B < width(B). A is unsigned.  */
    OP_SDIV,                /**< Signed division. Two operands, A/B. Result width is width(A). */
    OP_SET,                 /**< Set of expressions. Any number of operands in any order. */
    OP_SEXTEND,             /**< Signed extension at msb. Extend B to A bits by replicating B's most significant bit. */
    OP_SGE,                 /**< Signed greater-than-or-equal. Two operands of equal width. Result is Boolean. */
    OP_SGT,                 /**< Signed greater-than. Two operands of equal width. Result is Boolean. */
    OP_SHL0,                /**< Shift left, introducing zeros at lsb. Bits of B are shifted by A, where 0 <=A < width(B). */
    OP_SHL1,                /**< Shift left, introducing ones at lsb. Bits of B are shifted by A, where 0 <=A < width(B). */
    OP_SHR0,                /**< Shift right, introducing zeros at msb. Bits of B are shifted by A, where 0 <=A <width(B). */
    OP_SHR1,                /**< Shift right, introducing ones at msb. Bits of B are shifted by A, where 0 <=A <width(B). */
    OP_SLE,                 /**< Signed less-than-or-equal. Two operands of equal width. Result is Boolean. */
    OP_SLT,                 /**< Signed less-than. Two operands of equal width. Result is Boolean. */
    OP_SMOD,                /**< Signed modulus. Two operands, A%B. Result width is width(B). */
    OP_SMUL,                /**< Signed multiplication. Two operands A*B. Result width is width(A)+width(B). */
    OP_UDIV,                /**< Signed division. Two operands, A/B. Result width is width(A). */
    OP_UEXTEND,             /**< Unsigned extention at msb. Extend B to A bits by introducing zeros at the msb of B. */
    OP_UGE,                 /**< Unsigned greater-than-or-equal. Two operands of equal width. Boolean result. */
    OP_UGT,                 /**< Unsigned greater-than. Two operands of equal width. Result is Boolean. */
    OP_ULE,                 /**< Unsigned less-than-or-equal. Two operands of equal width. Result is Boolean. */
    OP_ULT,                 /**< Unsigned less-than. Two operands of equal width. Result is Boolean (1-bit vector). */
    OP_UMOD,                /**< Unsigned modulus. Two operands, A%B. Result width is width(B). */
    OP_UMUL,                /**< Unsigned multiplication. Two operands, A*B. Result width is width(A)+width(B). */
    OP_WRITE,               /**< Write (update) memory with a new value. Arguments are memory, address and value. */
    OP_XOR,                 /**< Bitwise exclusive disjunction. One or more operands, all the same width. */
    OP_ZEROP,               /**< Equal to zero. One operand. Result is a single bit, set iff A is equal to zero. */

    OP_BV_AND = OP_AND,                                 // [Robb Matzke 2017-11-14]: deprecated NO_STRINGIFY
    OP_BV_OR = OP_OR,                                   // [Robb Matzke 2017-11-14]: deprecated NO_STRINGIFY
    OP_BV_XOR = OP_XOR                                  // [Robb Matzke 2017-11-14]: deprecated NO_STRINGIFY
};

std::string toStr(Operator);

class Node;
class Interior;
class Leaf;
class ExprExprHashMap;

/** Shared-ownership pointer to an expression @ref Node. See @ref heap_object_shared_ownership. */
typedef Sawyer::SharedPointer<Node> Ptr;

/** Shared-ownership pointer to an expression @ref Interior node. See @ref heap_object_shared_ownership. */
typedef Sawyer::SharedPointer<Interior> InteriorPtr;

/** Shared-ownership pointer to an expression @ref Leaf node. See @ref heap_object_shared_ownership. */
typedef Sawyer::SharedPointer<Leaf> LeafPtr;

typedef std::vector<Ptr> Nodes;
typedef Map<uint64_t, uint64_t> RenameMap;

/** Hash of symbolic expression. */
typedef uint64_t Hash;

/** Controls formatting of expression trees when printing. */
struct Formatter {
    enum ShowComments {
        CMT_SILENT,                             /**< Do not show comments. */
        CMT_AFTER,                              /**< Show comments after the node. */
        CMT_INSTEAD,                            /**< Like CMT_AFTER, but show comments instead of variable names. */
    };
    Formatter()
        : show_comments(CMT_INSTEAD), do_rename(false), add_renames(true), use_hexadecimal(true),
          max_depth(0), cur_depth(0), show_width(true), show_flags(true) {}
    ShowComments show_comments;                 /**< Show node comments when printing? */
    bool do_rename;                             /**< Use the @p renames map to rename variables to shorter names? */
    bool add_renames;                           /**< Add additional entries to the @p renames as variables are encountered? */
    bool use_hexadecimal;                       /**< Show values in hexadecimal and decimal rather than just decimal. */
    size_t max_depth;                           /**< If non-zero, then replace deep parts of expressions with "...". */
    size_t cur_depth;                           /**< Depth in expression. */
    RenameMap renames;                          /**< Map for renaming variables to use smaller integers. */
    bool show_width;                            /**< Show width in bits inside square brackets. */
    bool show_flags;                            /**< Show user-defined flags inside square brackets. */
};

/** Return type for visitors. */
enum VisitAction {
    CONTINUE,                               /**< Continue the traversal as normal. */
    TRUNCATE,                               /**< For a pre-order depth-first visit, do not descend into children. */
    TERMINATE,                              /**< Terminate the traversal. */
};

/** Maximum number of nodes that can be reported.
 *
 *  If @ref nnodes returns this value then the size of the expressions could not be counted. This can happens when the
 *  expression contains a large number of common subexpressions. */
extern const uint64_t MAX_NNODES;       // defined in .C so we don't pollute user namespace with limit macros

/** Base class for visiting nodes during expression traversal.  The preVisit method is called before children are visited, and
 *  the postVisit method is called after children are visited.  If preVisit returns TRUNCATE, then the children are not
 *  visited, but the postVisit method is still called.  If either method returns TERMINATE then the traversal is immediately
 *  terminated. */
class Visitor {
public:
    virtual ~Visitor() {}
    virtual VisitAction preVisit(const Ptr&) = 0;
    virtual VisitAction postVisit(const Ptr&) = 0;
};



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                      Base Node Type
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Base class for symbolic expression nodes.
 *
 *  Every node has a specified width measured in bits that is constant over the life of the node. The width is always a
 *  concrete, positive value stored in a 64-bit field.  The corollary of this invariant is that if an expression's result
 *  width depends on the @em values of some of its arguments, those arguments must be concrete and not wider than 64 bits. Only
 *  a few operators fall into this category since most expressions depend on the @em widths of their arguments rather than the
 *  @em values of their arguments.
 *
 *  In order that subtrees can be freely assigned as children of other nodes (provided the structure as a whole remains a
 *  lattice and not a graph with cycles), two things are required: First, tree nodes are always referenced through
 *  shared-ownership pointers that collectively own the expression node (expressions are never explicitly deleted). Second,
 *  expression nodes are immutable once they're instantiated.  There are a handful of exceptions to the immutable rule:
 *  comments and attributes are allowed to change since they're not significant to hashing or arithmetic operations.
 *
 *  Each node has a bit flags property, the bits of which are defined by the user.  New nodes are created having all bits
 *  cleared unless the user specifies a value in the constructor.  Bits are significant for hashing. Simplifiers produce
 *  result expressions whose bits are set in a predictable manner with the following rules:
 *
 *  @li Interior Node Rule: The flags for an interior node are the union of the flags of its subtrees.
 *
 *  @li Simplification Discard Rule: If a simplification discards a subtree then that subtree does not contribute flags to the
 *      result.  E.g., cancellation of terms in an @c add operation.
 *
 *  @li Simplification Create Rule: If a simplification creates a new leaf node that doesn't depend on the input expression
 *      that new leaf node will have zero flags.  E.g., XOR of an expression with itself; an add operation where all the terms
 *      cancel each other resulting in zero.
 *
 *  @li Simplification Folding Rule: If a simplification creates a new expression from some combination of incoming expressions
 *      then the flags of the new expression are the union of the flags from the expressions on which it depends. E.g.,
 *      constant folding, which is therefore consistent with the Interior Node Rule.
 *
 *  @li Hashing Rule: User-defined flags are significant for hashing.  E.g., structural equivalence will return false if the
 *      two expressions have different flags since structural equivalence uses hashes.
 *
 *  @li Relational Operator Rule:  Simplification of relational operators to produce a Boolean constant will act as if they are
 *      performing constant folding even if the simplification is on variables.  E.g., <code>(ule v1 v1)</code> results in true
 *      with flags the same as @c v1. */
class Node
    : public Sawyer::SharedObject,
      public Sawyer::SharedFromThis<Node>,
      public Sawyer::SmallObject,
      public Sawyer::Attribute::Storage<> { // Attributes are not significant for hashing or arithmetic
protected:
    size_t nBits_;                    /**< Number of significant bits. Constant over the life of the node. */
    size_t domainWidth_;              /**< Width of domain for unary functions. E.g., memory. */
    unsigned flags_;                  /**< Bit flags. Meaning of flags is up to the user. Low-order 16 bits are reserved. */
    std::string comment_;             /**< Optional comment. Only for debugging; not significant for any calculation. */
    Hash hashval_;                    /**< Optional hash used as a quick way to indicate that two expressions are different. */
    boost::any userData_;             /**< Additional user-specified data. This is not part of the hash. */

#ifdef ROSE_HAVE_BOOST_SERIALIZATION_LIB
private:
    friend class boost::serialization::access;

    template<class S>
    void serialize(S &s, const unsigned /*version*/) {
        s & BOOST_SERIALIZATION_NVP(nBits_);
        s & BOOST_SERIALIZATION_NVP(domainWidth_);
        s & BOOST_SERIALIZATION_NVP(flags_);
        s & BOOST_SERIALIZATION_NVP(comment_);
        s & BOOST_SERIALIZATION_NVP(hashval_);
        // s & userData_;
    }
#endif

public:
    // Bit flags

    /** These flags are reserved for use within ROSE. */
    static const unsigned RESERVED_FLAGS = 0x0000ffff;

    /** Value is somehow indeterminate. E.g., read from writable memory. */
    static const unsigned INDETERMINATE  = 0x00000001;

    /** Value is somehow unspecified. A value that is intantiated as part of processing a machine instruction where the ISA
     * documentation is incomplete or says that some result is unspecified or undefined. Intel documentation for the x86 shift
     * and rotate instructions, for example, states that certain status bits have "undefined" values after the instruction
     * executes. */
    static const unsigned UNSPECIFIED    = 0x00000002;

    /** Value represents bottom in dataflow analysis.  If this flag is used by ROSE's dataflow engine to represent a bottom
     *  value in a lattice. */
    static const unsigned BOTTOM         = 0x00000004;

protected:
    Node()
        : nBits_(0), domainWidth_(0), flags_(0), hashval_(0) {}
    explicit Node(const std::string &comment, unsigned flags=0)
        : nBits_(0), domainWidth_(0), flags_(flags), comment_(comment), hashval_(0) {}

public:
    /** User-supplied predicate to augment alias checking.
     *
     * If this pointer is non-null, then the @ref mayEqual methods invoke this function. If this function returns true
     * or false, then its return value becomes the return value of @ref mayEqual, otherwise @ref mayEqual continues
     * as it normally would.  This user-defined function is invoked by @ref mayEqual after trivial situations are checked
     * and before any calls to an SMT solver. The SMT solver argument is optional (may be null). */
    static boost::logic::tribool (*mayEqualCallback)(const Ptr &a, const Ptr &b, const SmtSolverPtr&);
    
    /** Returns true if two expressions must be equal (cannot be unequal).
     *
     *  If an SMT solver is specified then that solver is used to answer this question, otherwise equality is established by
     *  looking only at the structure of the two expressions. Two expressions can be equal without being the same width (e.g.,
     *  a 32-bit constant zero is equal to a 16-bit constant zero). */
    virtual bool mustEqual(const Ptr &other, const SmtSolverPtr &solver = SmtSolverPtr()) = 0;

    /** Returns true if two expressions might be equal, but not necessarily be equal. */
    virtual bool mayEqual(const Ptr &other, const SmtSolverPtr &solver = SmtSolverPtr()) = 0;

    /** Tests two expressions for structural equivalence.
     *
     *  Two leaf nodes are equivalent if they are the same width and have equal values or are the same variable. Two interior
     *  nodes are equivalent if they are the same width, the same operation, have the same number of children, and those
     *  children are all pairwise equivalent. */
    virtual bool isEquivalentTo(const Ptr &other) = 0;

    /** Compare two expressions structurally for sorting.
     *
     *  Returns -1 if @p this is less than @p other, 0 if they are structurally equal, and 1 if @p this is greater than @p
     *  other.  This function returns zero when an only when @ref isEquivalentTo returns zero, but @ref isEquivalentTo can be
     *  much faster since it uses hashing. */
    virtual int compareStructure(const Ptr &other) = 0;

    /** Substitute one value for another.
     *
     *  Finds all occurrances of @p from in this expression and replace them with @p to. If a substitution occurs, then a new
     *  expression is returned. The matching of @p from to sub-parts of this expression uses structural equivalence, the
     *  @ref isEquivalentTo predicate. The @p from and @p to expressions must have the same width. */
    virtual Ptr substitute(const Ptr &from, const Ptr &to, const SmtSolverPtr &solver = SmtSolverPtr()) = 0;

    /** Rewrite expression by substituting subexpressions.
     *
     *  This expression is rewritten by doing a depth-first traversal. At each step of the traversal, the subexpression is
     *  looked up by hash in the supplied substitutions table. If found, a new expression is created using the value found in
     *  the table and the traversal does not descend into the new expression.  If no substitutions were performed then @p this
     *  expression is returned, otherwise a new expression is returned. An optional solver, which may be null, is used during
     *  the simplification step. */
    Ptr substituteMultiple(const ExprExprHashMap &substitutions, const SmtSolverPtr &solver = SmtSolverPtr());

    /** Rewrite using lowest numbered variable names.
     *
     *  Given an expression, use the specified index to rewrite variables. The index uses expression hashes to look up the
     *  replacement expression. If the traversal finds a variable which is not in the index then a new variable is created. The
     *  new variable has the same type as the original variable, but it's name is generated starting at @p nextVariableId and
     *  incrementing after each replacement is generated. The optional solver is used during the simplification process and may
     *  be null. */
    Ptr renameVariables(ExprExprHashMap &index /*in,out*/, size_t &nextVariableId /*in,out*/,
                        const SmtSolverPtr &solver = SmtSolverPtr());

    /** Returns true if the expression is a known numeric value.
     *
     *  The value itself is stored in the @ref number property. */
    virtual bool isNumber() = 0;

    /** Property: integer value of expression node.
     *
     *  Returns the integer value of a node for which @ref isKnown returns true.  The high-order bits, those beyond the number
     *  of significant bits returned by the @ref nBits propert, are guaranteed to be zero. */
    virtual uint64_t toInt() = 0;

    /** Property: Comment.
     *
     *  Comments can be changed after a node has been created since the comment is not intended to be used for anything but
     *  annotation and/or debugging. If many expressions are sharing the same node, then the comment is changed in all those
     *  expressions. Changing the comment property is allowed even though nodes are generally immutable because comments are
     *  not considered significant for comparisons, computing hash values, etc.
     *
     * @{ */
    const std::string& comment() { return comment_; }
    void comment(const std::string &s) { comment_ = s; }
    /** @} */

    /** Property: User-defined data.
     *
     *  User defined data is always optional and does not contribute to the hash value of an expression. The user-defined data
     *  can be changed at any time by the user even if the expression node to which it is attached is shared between many
     *  expressions.
     *
     * @{ */
    void userData(boost::any &data) {
        userData_ = data;
    }
    const boost::any& userData() {
        return userData_;
    }
    /** @} */

    /** Property: Number of significant bits.
     *
     *  An expression with a known value is guaranteed to have all higher-order bits cleared. */
    size_t nBits() { return nBits_; }

    /** Property: User-defined bit flags.
     *
     *  This property is significant for hashing, comparisons, and possibly other operations, therefore it is immutable.  To
     *  change the flags one must create a new expression; see @ref newFlags. */
    unsigned flags() { return flags_; }

    /** Sets flags. Since symbolic expressions are immutable it is not possible to change the flags directly. Therefore if the
     *  desired flags are different than the current flags a new expression is created that is the same in every other
     *  respect. If the flags are not changed then the original expression is returned. */
    Ptr newFlags(unsigned flags);

    /** Property: Width for memory expressions.
     *
     *  The return value is non-zero if and only if this tree node is a memory expression. */
    size_t domainWidth() { return domainWidth_; }

    /** Check whether expression is scalar.
     *
     *  Everything is scalar except for memory. */
    bool isScalar() { return 0 == domainWidth_; }

    /** Traverse the expression.
     *
     *  The expression is traversed in a depth-first visit.  The final return value is the final return value of the last call
     *  to the visitor. */
    virtual VisitAction depthFirstTraversal(Visitor&) = 0;

    /** Computes the size of an expression by counting the number of nodes.
     *
     *  Operates in constant time.  Note that it is possible (even likely) for the 64-bit return value to overflow in
     *  expressions when many nodes are shared.  For instance, the following loop will create an expression that contains more
     *  than 2^64 nodes:
     *
     *  @code
     *   SymbolicExpr expr = Leaf::createVariable(32);
     *   for(size_t i=0; i<64; ++i)
     *       expr = Interior::create(32, OP_ADD, expr, expr)
     *  @endcode
     *
     *  When an overflow occurs the result is meaningless.
     *
     *  @sa nNodesUnique */
    virtual uint64_t nNodes() = 0;

    /** Number of unique nodes in expression. */
    uint64_t nNodesUnique();

    /** Returns the variables appearing in the expression. */
    std::set<LeafPtr> getVariables();

    /** Dynamic cast of this object to an interior node.
     *
     *  Returns null if the cast is not valid. */
    InteriorPtr isInteriorNode() {
        return sharedFromThis().dynamicCast<Interior>();
    }

    /** Dynamic cast of this object to a leaf node.
     *
     *  Returns null if the cast is not valid. */
    LeafPtr isLeafNode() {
        return sharedFromThis().dynamicCast<Leaf>();
    }

    /** Returns true if this node has a hash value computed and cached. The hash value zero is reserved to indicate that no
     *  hash has been computed; if a node happens to actually hash to zero, it will not be cached and will be recomputed for
     *  every call to hash(). */
    bool isHashed() { return hashval_ != 0; }

    /** Returns (and caches) the hash value for this node.  If a hash value is not cached in this node, then a new hash value
     *  is computed and cached. */
    Hash hash();

    // used internally to set the hash value
    void hash(Hash);

    /** A node with formatter. See the with_format() method. */
    class WithFormatter {
    private:
        Ptr node;
        Formatter &formatter;
    public:
        WithFormatter(const Ptr &node, Formatter &formatter): node(node), formatter(formatter) {}
        void print(std::ostream &stream) const { node->print(stream, formatter); }
    };

    /** Combines a node with a formatter for printing.  This is used for convenient printing with the "<<" operator. For
     *  instance:
     *
     * @code
     *  Formatter fmt;
     *  fmt.show_comments = Formatter::CMT_AFTER; //show comments after the variable
     *  Ptr expression = ...;
     *  std::cout <<"method 1: "; expression->print(std::cout, fmt); std::cout <<"\n";
     *  std::cout <<"method 2: " <<expression->withFormat(fmt) <<"\n";
     *  std::cout <<"method 3: " <<*expression+fmt <<"\n";
     * 
     * @endcode
     * @{ */
    WithFormatter withFormat(Formatter &fmt) { return WithFormatter(sharedFromThis(), fmt); }
    WithFormatter operator+(Formatter &fmt) { return withFormat(fmt); }
    /** @} */


    /** Print the expression to a stream.  The output is an S-expression with no line-feeds. The format of the output is
     *  controlled by the mutable Formatter argument.
     *  @{ */
    virtual void print(std::ostream&, Formatter&) = 0;
    void print(std::ostream &o) { Formatter fmt; print(o, fmt); }
    /** @} */

    /** Asserts that expressions are acyclic. This is intended only for debugging. */
    void assertAcyclic();

    /** Find common subexpressions.
     *
     *  Returns a vector of the largest common subexpressions. The list is computed by performing a depth-first search on this
     *  expression and adding expressions to the return vector whenever a subtree is encountered a second time. Therefore the
     *  if a common subexpression A contains another common subexpression B then B will appear earlier in the list than A. */
    std::vector<Ptr> findCommonSubexpressions();

    /** Determine whether an expression is a variable plus a constant.
     *
     *  If this expression is of the form V + X or X + V where V is a variable and X is a constant, return true and make @p
     *  variable point to the variable and @p constant point to the constant.  If the expression is not one of these forms,
     *  then return false without modifying the arguments. */
    bool matchAddVariableConstant(LeafPtr &variable/*out*/, LeafPtr &constant/*out*/);

    /** True (non-null) if this node is the specified operator. */
    InteriorPtr isOperator(Operator);

protected:
    void printFlags(std::ostream &o, unsigned flags, char &bracket);
};

/** Operator-specific simplification methods. */
class Simplifier {
public:
    virtual ~Simplifier() {}

    /** Constant folding. The range @p begin (inclusive) to @p end (exclusive) must contain at least two nodes and all of
     *  the nodes must be leaf nodes with known values.  This method returns a new folded node if folding is possible, or
     *  the null pointer if folding is not possible. */
    virtual Ptr fold(Nodes::const_iterator /*begin*/, Nodes::const_iterator /*end*/) const {
        return Ptr();
    }

    /** Rewrite the entire expression to something simpler. Returns the new node if the node can be simplified, otherwise
     *  returns null. */
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const {
        return Ptr();
    }
};

struct ExprExprHashMapHasher {
     size_t operator()(const Ptr &expr) const {
        return expr->hash();
    }
};

struct ExprExprHashMapCompare {
    bool operator()(const Ptr &a, const Ptr &b) const {
        return a->isEquivalentTo(b);
    }
};

/** Compare two expressions for STL containers. */
class ExpressionLessp {
public:
    bool operator()(const Ptr &a, const Ptr &b);
};

/** Mapping from expression to expression. */
class ExprExprHashMap: public boost::unordered_map<SymbolicExpr::Ptr, SymbolicExpr::Ptr,
                                                   ExprExprHashMapHasher, ExprExprHashMapCompare> {
public:
    ExprExprHashMap invert() const;
};

/** Set of expressions ordered by hash. */
typedef Sawyer::Container::Set<Ptr, ExpressionLessp> ExpressionSet;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                      Simplification
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct AddSimplifier: Simplifier {
    virtual Ptr fold(Nodes::const_iterator, Nodes::const_iterator) const ROSE_OVERRIDE;
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct AndSimplifier: Simplifier {
    virtual Ptr fold(Nodes::const_iterator, Nodes::const_iterator) const ROSE_OVERRIDE;
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct OrSimplifier: Simplifier {
    virtual Ptr fold(Nodes::const_iterator, Nodes::const_iterator) const ROSE_OVERRIDE;
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct XorSimplifier: Simplifier {
    virtual Ptr fold(Nodes::const_iterator, Nodes::const_iterator) const ROSE_OVERRIDE;
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct SmulSimplifier: Simplifier {
    virtual Ptr fold(Nodes::const_iterator, Nodes::const_iterator) const ROSE_OVERRIDE;
};
struct UmulSimplifier: Simplifier {
    virtual Ptr fold(Nodes::const_iterator, Nodes::const_iterator) const ROSE_OVERRIDE;
};
struct ConcatSimplifier: Simplifier {
    virtual Ptr fold(Nodes::const_iterator, Nodes::const_iterator) const ROSE_OVERRIDE;
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct ExtractSimplifier: Simplifier {
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct AsrSimplifier: Simplifier {
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct InvertSimplifier: Simplifier {
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct NegateSimplifier: Simplifier {
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct IteSimplifier: Simplifier {
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct NoopSimplifier: Simplifier {
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct RolSimplifier: Simplifier {
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct RorSimplifier: Simplifier {
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct UextendSimplifier: Simplifier {
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct SextendSimplifier: Simplifier {
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct EqSimplifier: Simplifier {
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct SgeSimplifier: Simplifier {
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct SgtSimplifier: Simplifier {
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct SleSimplifier: Simplifier {
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct SltSimplifier: Simplifier {
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct UgeSimplifier: Simplifier {
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct UgtSimplifier: Simplifier {
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct UleSimplifier: Simplifier {
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct UltSimplifier: Simplifier {
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct ZeropSimplifier: Simplifier {
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct SdivSimplifier: Simplifier {
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct SmodSimplifier: Simplifier {
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct UdivSimplifier: Simplifier {
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct UmodSimplifier: Simplifier {
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct ShiftSimplifier: Simplifier {
    bool newbits;
    ShiftSimplifier(bool newbits): newbits(newbits) {}
    Ptr combine_strengths(Ptr strength1, Ptr strength2, size_t value_width, const SmtSolverPtr &solver) const;
};
struct ShlSimplifier: ShiftSimplifier {
    ShlSimplifier(bool newbits): ShiftSimplifier(newbits) {}
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct ShrSimplifier: ShiftSimplifier {
    ShrSimplifier(bool newbits): ShiftSimplifier(newbits) {}
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct LssbSimplifier: Simplifier {
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct MssbSimplifier: Simplifier {
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};
struct SetSimplifier: Simplifier {
    virtual Ptr rewrite(Interior*, const SmtSolverPtr&) const ROSE_OVERRIDE;
};



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                      Interior Nodes
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Interior node of an expression tree for instruction semantics. Each interior node has an operator (constant for the life of
 *  the node and obtainable with get_operator()) and zero or more children. Children are added to the interior node during the
 *  construction phase. Once construction is complete, the children should only change in ways that don't affect the value of
 *  the node as a whole (since this node might be pointed to by any number of expressions). */
class Interior: public Node {
private:
    Operator op_;
    Nodes children_;
    uint64_t nnodes_;                                   // total number of nodes; self + children's nnodes

    // Constructors should not be called directly.  Use the create() class method instead. This is to help prevent
    // accidently using pointers to these objects -- all access should be through shared-ownership pointers.
    Interior(): op_(OP_ADD), nnodes_(1) {} // needed for serialization
    Interior(size_t nbits, Operator op, const Ptr &a, const std::string &comment="", unsigned flags=0);
    Interior(size_t nbits, Operator op, const Ptr &a, const Ptr &b, const std::string &comment="", unsigned flags=0);
    Interior(size_t nbits, Operator op, const Ptr &a, const Ptr &b, const Ptr &c, const std::string &comment="",
             unsigned flags=0);
    Interior(size_t nbits, Operator op, const Nodes &children, const std::string &comment="", unsigned flags=0);

#ifdef ROSE_HAVE_BOOST_SERIALIZATION_LIB
private:
    friend class boost::serialization::access;

    template<class S>
    void serialize(S &s, const unsigned /*version*/) {
        s & BOOST_SERIALIZATION_BASE_OBJECT_NVP(Node);
        s & BOOST_SERIALIZATION_NVP(op_);
        s & BOOST_SERIALIZATION_NVP(children_);
        s & BOOST_SERIALIZATION_NVP(nnodes_);
    }
#endif

public:
    /** Create a new expression node.
     *
     *  Although we're creating interior nodes, the simplification process might replace it with a leaf node. Use these class
     *  methods instead of c'tors.
     *
     *  The SMT solver is optional and may be the null pointer.
     *
     *  Flags are normally initialized as the union of the flags of the operator arguments subject to various rules in the
     *  expression simplifiers. Flags specified in the constructor are set in addition to those that would normally be set.
     *
     *  @{ */
    static Ptr create(size_t nbits, Operator op, const Ptr &a,
                      const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0) {
        InteriorPtr retval(new Interior(nbits, op, a, comment, flags));
        return retval->simplifyTop(solver);
    }
    static Ptr create(size_t nbits, Operator op, const Ptr &a, const Ptr &b,
                      const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0) {
        InteriorPtr retval(new Interior(nbits, op, a, b, comment, flags));
        return retval->simplifyTop(solver);
    }
    static Ptr create(size_t nbits, Operator op, const Ptr &a, const Ptr &b, const Ptr &c,
                      const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0) {
        InteriorPtr retval(new Interior(nbits, op, a, b, c, comment, flags));
        return retval->simplifyTop(solver);
    }
    static Ptr create(size_t nbits, Operator op, const Nodes &children,
                      const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0) {
        InteriorPtr retval(new Interior(nbits, op, children, comment, flags));
        return retval->simplifyTop(solver);
    }
    /** @} */

    /* see superclass, where these are pure virtual */
    virtual bool mustEqual(const Ptr &other, const SmtSolverPtr &solver = SmtSolverPtr()) ROSE_OVERRIDE;
    virtual bool mayEqual(const Ptr &other, const SmtSolverPtr &solver = SmtSolverPtr()) ROSE_OVERRIDE;
    virtual bool isEquivalentTo(const Ptr &other) ROSE_OVERRIDE;
    virtual int compareStructure(const Ptr& other) ROSE_OVERRIDE;
    virtual Ptr substitute(const Ptr &from, const Ptr &to, const SmtSolverPtr &solver = SmtSolverPtr()) ROSE_OVERRIDE;
    virtual bool isNumber() ROSE_OVERRIDE {
        return false; /*if it's known, then it would have been folded to a leaf*/
    }
    virtual uint64_t toInt() ROSE_OVERRIDE { ASSERT_forbid2(true, "not a number"); return 0;}
    virtual VisitAction depthFirstTraversal(Visitor&) ROSE_OVERRIDE;
    virtual uint64_t nNodes() ROSE_OVERRIDE { return nnodes_; }

    /** Returns the number of children. */
    size_t nChildren() { return children_.size(); }

    /** Returns the specified child. */
    Ptr child(size_t idx) { ASSERT_require(idx<children_.size()); return children_[idx]; }

    /** Property: Children.
     *
     *  The children are the operands for an operator expression. */
    const Nodes& children() { return children_; }

    /** Returns the operator. */
    Operator getOperator() { return op_; }

    /** Simplifies the specified interior node.
     *
     *  Returns a new node if necessary, otherwise returns this. The SMT solver is optional and my be the null pointer. */
    Ptr simplifyTop(const SmtSolverPtr &solver = SmtSolverPtr());

    /** Perform constant folding.  This method returns either a new expression (if changes were mde) or the original
     *  expression. The simplifier is specific to the kind of operation at the node being simplified. */
    Ptr foldConstants(const Simplifier&);

    /** Simplifies non-associative operators by flattening the specified interior node with its children that are the same
     *  interior node type. Call this only if the top node is a truly non-associative. A new node is returned only if
     *  changed. When calling both nonassociative and commutative, it's usually more appropriate to call nonassociative
     *  first. */
    InteriorPtr associative();

    /** Simplifies commutative operators by sorting arguments. The arguments are sorted so that all the interior nodes come
     *  before the leaf nodes. Call this only if the top node is truly commutative.  A new node is returned only if
     *  changed. When calling both nonassociative and commutative, it's usually more appropriate to call nonassociative
     *  first. */
    InteriorPtr commutative();

    /** Simplifies involutary operators.  An involutary operator is one that is its own inverse.  This method should only be
     *  called if this node is an interior node whose operator has the involutary property (such as invert or negate). Returns
     *  either a new expression that is simplified, or the original expression. */
    Ptr involutary();

    /** Simplifies nested shift-like operators.
     *
     *  Simplifies (shift AMT1 (shift AMT2 X)) to (shift (add AMT1 AMT2) X). The SMT solver may be null. */
    Ptr additiveNesting(const SmtSolverPtr &solver = SmtSolverPtr());

    /** Removes identity arguments.
     *
     *  Returns either a new expression or the original expression. The solver may be a null pointer. */
    Ptr identity(uint64_t ident, const SmtSolverPtr &solver = SmtSolverPtr());

    /** Replaces a binary operator with its only argument. Returns either a new expression or the original expression. */
    Ptr unaryNoOp();

    /** Simplify an interior node. Returns a new node if this node could be simplified, otherwise returns this node. When
     *  the simplification could result in a leaf node, we return an OP_NOOP interior node instead. The SMT solver is optional
     *  and may be the null pointer. */
    Ptr rewrite(const Simplifier &simplifier, const SmtSolverPtr &solver = SmtSolverPtr());

    virtual void print(std::ostream&, Formatter&) ROSE_OVERRIDE;

protected:
    /** Appends @p child as a new child of this node. This must only be called from constructors. */
    void addChild(const Ptr &child);

    /** Adjust width based on operands. This must only be called from constructors. */
    void adjustWidth();

    /** Adjust user-defined bit flags. This must only be called from constructors.  Flags are the union of the operand flags
     *  subject to simplification rules, unioned with the specified flags. */
    void adjustBitFlags(unsigned extraFlags);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                      Leaf Nodes
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Leaf node of an expression tree for instruction semantics.
 *
 *  A leaf node is either a known bit vector value, a free bit vector variable, or a memory state. */
class Leaf: public Node {
private:
    enum LeafType { CONSTANT, BITVECTOR, MEMORY };
    LeafType leafType_;
    Sawyer::Container::BitVector bits_; /**< Value when 'known' is true */
    uint64_t name_;                     /**< Variable ID number when 'known' is false. */

#ifdef ROSE_HAVE_BOOST_SERIALIZATION_LIB
private:
    friend class boost::serialization::access;

    template<class S>
    void save(S &s, const unsigned /*version*/) const {
        s & BOOST_SERIALIZATION_BASE_OBJECT_NVP(Node);
        s & BOOST_SERIALIZATION_NVP(leafType_);
        s & BOOST_SERIALIZATION_NVP(bits_);
        s & BOOST_SERIALIZATION_NVP(name_);
    }

    template<class S>
    void load(S &s, const unsigned /*version*/) {
        s & BOOST_SERIALIZATION_BASE_OBJECT_NVP(Node);
        s & BOOST_SERIALIZATION_NVP(leafType_);
        s & BOOST_SERIALIZATION_NVP(bits_);
        s & BOOST_SERIALIZATION_NVP(name_);
        nextNameCounter(name_);
    }

    BOOST_SERIALIZATION_SPLIT_MEMBER();
#endif

    // Private to help prevent creating pointers to leaf nodes.  See create_* methods instead.
private:
    Leaf()
        : Node(""), leafType_(CONSTANT), name_(0) {}
    explicit Leaf(const std::string &comment, unsigned flags=0)
        : Node(comment, flags), leafType_(CONSTANT), name_(0) {}

public:
    /** Construct a new free variable with a specified number of significant bits. */
    static LeafPtr createVariable(size_t nbits, const std::string &comment="", unsigned flags=0);

    /** Construct another reference to an existing variable.  This method is used internally by the expression parsing
     *  mechanism to produce a new instance of some previously existing variable -- both instances are the same variable and
     *  therefore should be given the same size (although this consistency cannot be checked automatically). */
    static LeafPtr createExistingVariable(size_t nbits, uint64_t id, const std::string &comment="", unsigned flags=0);

    /** Construct a new integer with the specified number of significant bits. Any high-order bits beyond the specified size
     *  will be zeroed. */
    static LeafPtr createInteger(size_t nbits, uint64_t n, const std::string &comment="", unsigned flags=0);

    /** Construct a new known value with the specified bits. */
    static LeafPtr createConstant(const Sawyer::Container::BitVector &bits, const std::string &comment="", unsigned flags=0);

    /** Create a new Boolean, a single-bit integer. */
    static LeafPtr createBoolean(bool b, const std::string &comment="", unsigned flags=0) {
        return createInteger(1, (uint64_t)(b?1:0), comment, flags);
    }

    /** Construct a new memory state.  A memory state is a function that maps addresses to values. */
    static LeafPtr createMemory(size_t addressWidth, size_t valueWidth, const std::string &comment="", unsigned flags=0);

    /** Construct another reference to an existing variable.  This method is used internally by the expression parsing
     * mechanism to produce a new instance of some previously existing memory state. Both instances are the same state and
     * therefore should be given the same domain and range size (although this consistency cannot be checked automatically.) */
    static LeafPtr createExistingMemory(size_t addressWidth, size_t valueWidth, uint64_t id, const std::string &comment="",
                                        unsigned flags=0);

    // from base class
    virtual bool isNumber() ROSE_OVERRIDE;
    virtual uint64_t toInt() ROSE_OVERRIDE;
    virtual bool mustEqual(const Ptr &other, const SmtSolverPtr &solver = SmtSolverPtr()) ROSE_OVERRIDE;
    virtual bool mayEqual(const Ptr &other, const SmtSolverPtr &solver = SmtSolverPtr()) ROSE_OVERRIDE;
    virtual bool isEquivalentTo(const Ptr &other) ROSE_OVERRIDE;
    virtual int compareStructure(const Ptr& other) ROSE_OVERRIDE;
    virtual Ptr substitute(const Ptr &from, const Ptr &to, const SmtSolverPtr &solver = SmtSolverPtr()) ROSE_OVERRIDE;
    virtual VisitAction depthFirstTraversal(Visitor&) ROSE_OVERRIDE;
    virtual uint64_t nNodes() ROSE_OVERRIDE { return 1; }

    /** Property: Bits stored for numeric values. */
    const Sawyer::Container::BitVector& bits();

    /** Is the node a bitvector variable? */
    virtual bool isVariable();

    /** Does the node represent memory? */
    virtual bool isMemory();

    /** Returns the name ID of a free variable.
     *
     *  The output functions print variables as "vN" where N is an integer. It is this N that this method returns.  It should
     *  only be invoked on leaf nodes for which @ref isNumber returns false. */
    uint64_t nameId();

    /** Returns a string for the leaf.
     *
     *  Variables are returned as "vN", memory is returned as "mN", and constants are returned as a hexadecimal string, where N
     *  is a variable or memory number. */
    std::string toString();

    // documented in super class
    virtual void print(std::ostream&, Formatter&) ROSE_OVERRIDE;

    /** Prints an integer interpreted as a signed value. */
    void printAsSigned(std::ostream&, Formatter&, bool asSigned=true);

    /** Prints an integer interpreted as an unsigned value. */
    void printAsUnsigned(std::ostream &o, Formatter &f) {
        printAsSigned(o, f, false);
    }

private:
    // Obtain or register a name ID
    static uint64_t nextNameCounter(uint64_t useThis = (uint64_t)(-1));
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                      Factories
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/** Leaf constructor.
 *
 *  Constructs an expression leaf node. This is a wrapper around one of the "create" factory methods in @ref Leaf.
 *
 * @{ */
Ptr makeVariable(size_t nbits, const std::string &comment="", unsigned flags=0);
Ptr makeExistingVariable(size_t nbits, uint64_t id, const std::string &comment="", unsigned flags=0);
Ptr makeInteger(size_t nbits, uint64_t n, const std::string &comment="", unsigned flags=0);
Ptr makeConstant(const Sawyer::Container::BitVector&, const std::string &comment="", unsigned flags=0);
Ptr makeBoolean(bool, const std::string &comment="", unsigned flags=0);
Ptr makeMemory(size_t addressWidth, size_t valueWidth, const std::string &comment="", unsigned flags=0);
Ptr makeExistingMemory(size_t addressWidth, size_t valueWidth, uint64_t id, const std::string &comment="", unsigned flags=0);
/** @} */

/** Interior node constructor.
 *
 *  Constructs an interior node. This is a wrapper around one of the "create" factory methods in @ref Interior. It
 *  interprets its operands as unsigned values unless the method has "Signed" in its name.
 *
 * @{ */
Ptr makeAdd(const Ptr&a, const Ptr &b,
            const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeBooleanAnd(const Ptr &a, const Ptr &b,
                   const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0)
                   ROSE_DEPRECATED("use makeAnd instead"); // [Robb Matzke 2017-11-21]: deprecated
Ptr makeAsr(const Ptr &sa, const Ptr &a,
            const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeAnd(const Ptr &a, const Ptr &b,
            const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeOr(const Ptr &a, const Ptr &b,
           const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeXor(const Ptr &a, const Ptr &b,
            const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeConcat(const Ptr &hi, const Ptr &lo,
               const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeEq(const Ptr &a, const Ptr &b,
           const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeExtract(const Ptr &begin, const Ptr &end, const Ptr &a,
                const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeInvert(const Ptr &a,
               const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeIte(const Ptr &cond, const Ptr &a, const Ptr &b,
            const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeLet(const Ptr &a, const Ptr &b, const Ptr &c,
            const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeLssb(const Ptr &a,
             const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeMssb(const Ptr &a,
             const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeNe(const Ptr &a, const Ptr &b,
           const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeNegate(const Ptr &a,
               const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeBooleanOr(const Ptr &a, const Ptr &b,
                  const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0)
                  ROSE_DEPRECATED("use makeOr instead"); // [Robb Matzke 2017-11-21]: deprecated
Ptr makeRead(const Ptr &mem, const Ptr &addr,
             const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeRol(const Ptr &sa, const Ptr &a,
            const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeRor(const Ptr &sa, const Ptr &a,
            const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeSet(const Ptr &a, const Ptr &b,
            const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeSet(const Ptr &a, const Ptr &b, const Ptr &c,
            const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeSignedDiv(const Ptr &a, const Ptr &b,
                  const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeSignExtend(const Ptr &newSize, const Ptr &a,
                   const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeSignedGe(const Ptr &a, const Ptr &b,
                 const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeSignedGt(const Ptr &a, const Ptr &b,
                 const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeShl0(const Ptr &sa, const Ptr &a,
             const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeShl1(const Ptr &sa, const Ptr &a,
             const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeShr0(const Ptr &sa, const Ptr &a,
             const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeShr1(const Ptr &sa, const Ptr &a,
             const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeSignedLe(const Ptr &a, const Ptr &b,
                 const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeSignedLt(const Ptr &a, const Ptr &b,
                 const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeSignedMod(const Ptr &a, const Ptr &b,
                  const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeSignedMul(const Ptr &a, const Ptr &b,
                  const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeDiv(const Ptr &a, const Ptr &b,
            const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeExtend(const Ptr &newSize, const Ptr &a,
               const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeGe(const Ptr &a, const Ptr &b,
           const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeGt(const Ptr &a, const Ptr &b,
           const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeLe(const Ptr &a, const Ptr &b,
           const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeLt(const Ptr &a, const Ptr &b,
           const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeMod(const Ptr &a, const Ptr &b,
            const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeMul(const Ptr &a, const Ptr &b,
            const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeWrite(const Ptr &mem, const Ptr &addr, const Ptr &a,
              const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
Ptr makeZerop(const Ptr &a,
              const SmtSolverPtr &solver = SmtSolverPtr(), const std::string &comment="", unsigned flags=0);
/** @} */


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//                                      Miscellaneous functions
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


std::ostream& operator<<(std::ostream &o, Node&);
std::ostream& operator<<(std::ostream &o, const Node::WithFormatter&);

/** Convert a set to an ite expression. */
Ptr setToIte(const Ptr&, const SmtSolverPtr &solver = SmtSolverPtr(), const LeafPtr &var = LeafPtr());

/**  Hash zero or more expressions.
 *
 *   Computes the hash for each expression, then returns a single has which is a function of the individual hashes. The
 *   order of the expressions does not affect the returned hash. */
Hash hash(const std::vector<Ptr>&);

/** Counts the number of nodes.
 *
 *  Counts the total number of nodes in multiple expressions.  The return value is a saturated sum, returning MAX_NNODES if an
 *  overflow occurs. */
template<typename InputIterator>
uint64_t
nNodes(InputIterator begin, InputIterator end) {
    uint64_t total = 0;
    for (InputIterator ii=begin; ii!=end; ++ii) {
        uint64_t n = (*ii)->nnodes();
        if (MAX_NNODES==n)
            return MAX_NNODES;
        if (total + n < total)
            return MAX_NNODES;
        total += n;
    }
    return total;
}

/** Counts the number of unique nodes.
 *
 *  Counts the number of unique nodes across a number of expressions.  Nodes shared between two expressions are counted only
 *  one time, whereas the Node::nnodes virtual method counts shared nodes multiple times. */
template<typename InputIterator>
uint64_t
nNodesUnique(InputIterator begin, InputIterator end)
{
    struct T1: Visitor {
        typedef std::set<const Node*> SeenNodes;

        SeenNodes seen;                                 // nodes that we've already seen, and the subtree size
        uint64_t nUnique;                               // number of unique nodes

        T1(): nUnique(0) {}

        VisitAction preVisit(const Ptr &node) {
            if (seen.insert(getRawPointer(node)).second) {
                ++nUnique;
                return CONTINUE;                        // this node has not been seen before; traverse into children
            } else {
                return TRUNCATE;                        // this node has been seen already; skip over the children
            }
        }

        VisitAction postVisit(const Ptr &node) {
            return CONTINUE;
        }
    } visitor;

    VisitAction status = CONTINUE;
    for (InputIterator ii=begin; ii!=end && TERMINATE!=status; ++ii)
        status = (*ii)->depthFirstTraversal(visitor);
    return visitor.nUnique;
}

/** Find common subexpressions.
 *
 *  This is similar to @ref Node::findCommonSubexpressions except the analysis is over a collection of expressions
 *  rather than a single expression.
 *
 * @{ */
std::vector<Ptr> findCommonSubexpressions(const std::vector<Ptr>&);

template<typename InputIterator>
std::vector<Ptr>
findCommonSubexpressions(InputIterator begin, InputIterator end) {
    typedef Sawyer::Container::Map<Ptr, size_t> NodeCounts;
    struct T1: Visitor {
        NodeCounts nodeCounts;
        std::vector<Ptr> result;

        VisitAction preVisit(const Ptr &node) ROSE_OVERRIDE {
            size_t &nSeen = nodeCounts.insertMaybe(node, 0);
            if (2 == ++nSeen)
                result.push_back(node);
            return nSeen>1 ? TRUNCATE : CONTINUE;
        }

        VisitAction postVisit(const Ptr&) ROSE_OVERRIDE {
            return CONTINUE;
        }
    } visitor;

    for (InputIterator ii=begin; ii!=end; ++ii)
        (*ii)->depthFirstTraversal(visitor);
    return visitor.result;
}
/** @} */

/** On-the-fly substitutions.
 *
 *  This function uses a user-defined substitutor to generate values that are substituted into the specified expression. This
 *  operates by performing a depth-first search of the specified expression and calling the @p subber at each node. The @p
 *  subber is invoked with two arguments: an expression to be replaced, and an optional SMT solver for simplifications. It
 *  should return either the expression unmodified, or a new expression.  The return value of the @c substitute function as a
 *  whole is either the original expression (if no substitutions were performed) or a new expression. */
template<class Substitution>
Ptr substitute(const Ptr &src, Substitution &subber, const SmtSolverPtr &solver = SmtSolverPtr()) {
    if (!src)
        return Ptr();                                   // no input implies no output

    // Try substituting the whole expression, returning the result.
    Ptr dst = subber(src, solver);
    ASSERT_not_null(dst);
    if (dst != src)
        return dst;

    // Try substituting all the subexpressions.
    InteriorPtr inode = src->isInteriorNode();
    if (!inode)
        return src;
    bool anyChildChanged = false;
    Nodes newChildren;
    newChildren.reserve(inode->nChildren());
    BOOST_FOREACH (const Ptr &child, inode->children()) {
        Ptr newChild = substitute(child, subber, solver);
        if (newChild != child)
            anyChildChanged = true;
        newChildren.push_back(newChild);
    }
    if (!anyChildChanged)
        return src;

    // Some subexpression changed, so build a new expression
    return Interior::create(0, inode->getOperator(), newChildren, solver, inode->comment(), inode->flags());
}

} // namespace
} // namespace
} // namespace

#ifdef ROSE_HAVE_BOOST_SERIALIZATION_LIB
BOOST_CLASS_EXPORT_KEY(Rose::BinaryAnalysis::SymbolicExpr::Interior);
BOOST_CLASS_EXPORT_KEY(Rose::BinaryAnalysis::SymbolicExpr::Leaf);
#endif

#endif
