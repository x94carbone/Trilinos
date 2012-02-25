/*------------------------------------------------------------------------*/
/*                 Copyright 2010 Sandia Corporation.                     */
/*  Under terms of Contract DE-AC04-94AL85000, there is a non-exclusive   */
/*  license for use of this work by or on behalf of the U.S. Government.  */
/*  Export of this program may require a license from the                 */
/*  United States Government.                                             */
/*------------------------------------------------------------------------*/


#ifndef stk_mesh_Selector_hpp
#define stk_mesh_Selector_hpp

#include <iosfwd>
#include <stk_mesh/base/Types.hpp>

namespace stk {
namespace mesh {

enum Op{
        INVALID = 0,
        COMPOUND = 1,
        PART_ID = 2
       };

struct OpType {
  unsigned       m_part_id ; ///< Id of part under consideration
  unsigned short m_unary ;   ///< Unary NOT operator: m_unary ^ expression
  unsigned short m_count ;   ///< Compound statement length
  Op             m_op      ; ///< Does the OpType reference a part

  OpType() : m_part_id(0), m_unary(0), m_count(0), m_op(INVALID) {}
  OpType( unsigned part_id , unsigned unary , unsigned count, Op op=INVALID )
    : m_part_id( part_id ), m_unary( unary ), m_count( count ), m_op(op)  {}

  bool operator == (const OpType & opType ) const
  {
    return m_part_id == opType.m_part_id &&
           m_unary == opType.m_unary &&
           m_count == opType.m_count &&
           m_op == opType.m_op;
  }
  bool operator != (const OpType & opType ) const
  { return !(*this == opType); }
};

/** \addtogroup stk_mesh_module
 *  \{
 */

/** \brief This is a class for selecting \ref stk::mesh::Bucket "buckets" based on a set of
 * \ref stk::mesh::Part "meshparts" and set logic.
 *
 * The selector allows complements, unions and intersections.  All of
 * this logic is converted to NAND, meaning nots and AND logic.  Each
 * operation is placed on a stack of operands where each operand is
 * either a left parenthesis with a number of operands included in the
 * compound object, or an actual meshpart.  All operands have a unary
 * bit used to complement the operand.
 *
 * Please see the \ref stk_mesh_selector_unit "unit testing" for additional documentation.
 *
 * **/

class Selector {
public:
  /**  \brief  A default Selector selects nothing */
  Selector();

  bool operator == (const Selector & rhs) const
  { return m_op == rhs.m_op; }

  bool operator != (const Selector & rhs) const
  { return m_op != rhs.m_op; }

  /** \brief  A part that is required */
  Selector( const Part & part);

  /** \brief  Intersection: this = this INTERSECT ( expression ) */
  Selector & operator &= ( const Selector & selector);

  /** \brief  Union: this = this UNION ( expression ) */
  Selector & operator |= ( const Selector & selector);

  /** \brief  Complement: this = !(this)
   * Postcondition:  this is a compound expression
   * */
  Selector & complement();

  /** \brief Complement:  return !(this) */
  Selector operator ! () const
    { Selector S( *this ); return S.complement(); }

  /** \brief  Is this bucket a subset of the
   *          set defined by the selector expression.
   */
  bool operator()( const Bucket & candidate ) const;

  /** \brief  Is this bucket a subset of the
   *          set defined by the selector expression.
   */
  bool operator()( const Bucket * candidate ) const;

  /** \brief  Is this entity a member of the
   *          set defined by the selector expression.
   */
  bool operator()( const Entity & candidate ) const;

  /** \brief Is the intersection of the 'part_ords' parts a member
   * of the set defined by the selector expression.
   */
  bool apply(const std::pair<const unsigned*,const unsigned*>& part_ords) const
  { return apply(m_op.begin(), m_op.end(), part_ords); }

  /** \brief  Pretty print the set-expression with part names */
  friend std::ostream & operator << ( std::ostream & out, const Selector & selector);

  const std::vector<OpType>& get_ops() const { return m_op; }
  void set_ops(const std::vector<OpType>& ops) { m_op = ops; }

  /** \brief Turn the entire expression into a compound */
  void compoundAll();

private:

  /** \brief . */
  const MetaData * m_mesh_meta_data ;

  /** \brief . */
  std::vector< OpType > m_op ;

  /** \brief . */
  void verify_compatible( const Selector & B ) const;

  /** \brief . */
  void verify_compatible( const Bucket & B ) const;

  /** \brief . */
  bool part_is_present(
      unsigned part_ord ,
      const std::pair<const unsigned*,const unsigned*>& part_ords
      ) const;

  /** \brief . */
  bool apply(
      std::vector<OpType>::const_iterator start,
      std::vector<OpType>::const_iterator finish,
      const std::pair<const unsigned*,const unsigned*>& part_ords
      ) const;

  /** \brief Pretty print the expression */
  std::string printExpression(
      const std::vector<OpType>::const_iterator start,
      const std::vector<OpType>::const_iterator finish
      ) const;

};

std::ostream & operator<<( std::ostream & out, const Selector & selector);

/** \brief .
 * \relates Selector
 * */
Selector operator & ( const Part & A , const Part & B );

/** \brief .
 * \relates Selector
 * */
Selector operator & ( const Part & A , const Selector & B );

/** \brief .
 * \relates Selector
 * */
Selector operator & ( const Selector & A, const Part & B );

/** \brief .
 * \relates Selector
 * */
Selector operator & ( const Selector & A, const Selector & B );

/** \brief .
 * \relates Selector
 * */
Selector operator | ( const Part & A , const Part & B );

/** \brief .
 * \relates Selector
 * */
Selector operator | ( const Part & A , const Selector & B );

/** \brief .
 * \relates Selector
 * */
Selector operator | ( const Selector & A, const Part & B  );

/** \brief .
 * \relates Selector
 * */
Selector operator | ( const Selector & A , const Selector & B );

/** \brief .
 * \relates Selector
 * */
Selector operator ! ( const Part & A );


/** \brief .
 * \relates Selector
 * */
Selector selectUnion( const PartVector& union_part_vector );

/** \brief .
 * \relates Selector
 * */
Selector selectIntersection( const PartVector& intersection_part_vector );

/** \brief Return a selector for the union of the parts where field exists.
 * \relates Selector
 * */
Selector selectField( const FieldBase& field );

/** \} */

} // namespace mesh
} // namespace stk

#endif // stk_mesh_Selector_hpp

