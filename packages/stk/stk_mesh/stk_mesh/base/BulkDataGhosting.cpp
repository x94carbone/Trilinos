/*------------------------------------------------------------------------*/
/*                 Copyright 2010 Sandia Corporation.                     */
/*  Under terms of Contract DE-AC04-94AL85000, there is a non-exclusive   */
/*  license for use of this work by or on behalf of the U.S. Government.  */
/*  Export of this program may require a license from the                 */
/*  United States Government.                                             */
/*------------------------------------------------------------------------*/


/**
 * @author H. Carter Edwards
 */

#include <cstring>
#include <set>
#include <stdexcept>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <assert.h>

#include <stk_util/parallel/ParallelComm.hpp>
#include <stk_util/parallel/ParallelReduce.hpp>

#include <stk_mesh/base/Ghosting.hpp>
#include <stk_mesh/base/BulkData.hpp>
#include <stk_mesh/base/MetaData.hpp>
#include <stk_mesh/base/FieldData.hpp>
#include <stk_mesh/base/EntityComm.hpp>
#include <stk_mesh/base/Comm.hpp>

using std::strcmp;

namespace stk {
namespace mesh {

//----------------------------------------------------------------------

Ghosting & BulkData::create_ghosting( const std::string & name )
{
  static const char method[] = "stk::mesh::BulkData::create_ghosting" ;

  assert_ok_to_modify( method );

  // Verify name is the same on all processors,
  // if not then throw an exception on all processors.
  {
    CommBroadcast bc( parallel() , 0 );

    if ( bc.parallel_rank() == 0 ) {
      bc.send_buffer().skip<char>( name.size() + 1 );
    }

    bc.allocate_buffer();

    if ( bc.parallel_rank() == 0 ) {
      bc.send_buffer().pack<char>( name.c_str() , name.size() + 1 );
    }

    bc.communicate();

    const char * const bc_name =
      reinterpret_cast<const char *>( bc.recv_buffer().buffer() );

    int error = 0 != strcmp( bc_name , name.c_str() );

    all_reduce( parallel() , ReduceMax<1>( & error ) );

    if ( error ) {
      std::string msg("stk::mesh::BulkData::create_ghosting ERROR: Parallel name inconsistency");
      throw std::runtime_error( msg );
    }
  }

  Ghosting * const g =
    new Ghosting( *this , name , m_ghosting.size() , m_sync_count );

  m_ghosting.push_back( g );

  return *g ;
}

//----------------------------------------------------------------------
//----------------------------------------------------------------------

namespace {

void insert_transitive_closure( std::set<EntityProc,EntityLess> & new_send ,
                                const EntityProc & entry );

void comm_recv_to_send(
  BulkData & mesh ,
  const std::set< Entity * , EntityLess > & new_recv ,
        std::set< EntityProc , EntityLess > & new_send );

void comm_sync_send_recv(
  BulkData & mesh ,
  std::set< EntityProc , EntityLess > & new_send ,
  std::set< Entity * , EntityLess > & new_recv );

} // namespace <>

//----------------------------------------------------------------------
//----------------------------------------------------------------------

void BulkData::destroy_all_ghosting()
{
  static const char method[] = "stk::mesh::BulkData::destroy_all_ghosting" ;

  assert_ok_to_modify( method );

  // Clear Ghosting data

  for ( std::vector<Ghosting*>::iterator
        ig = m_ghosting.begin() ; ig != m_ghosting.end() ; ++ig ) {
    Ghosting & gh = **ig ;
    gh.m_sync_count = m_sync_count ;
  }

  // Iterate backwards so as not to invalidate a closure.

  std::vector<Entity*>::iterator ie = m_entity_comm.end();

  while ( ie != m_entity_comm.begin() ) {

    Entity * entity = *--ie ;

    if ( in_receive_ghost( *entity ) ) {
      entity->m_comm.clear();
      destroy_entity( entity );
      *ie = NULL ;
    }
    else {
      std::vector< EntityCommInfo >::iterator j = entity->m_comm.begin();
      while ( j != entity->m_comm.end() && j->ghost_id == 0 ) { ++j ; }
      entity->m_comm.erase( j , entity->m_comm.end() );
      if ( entity->m_comm.empty() ) {
        *ie = NULL ;
      }
    }
  }

  ie = std::remove( m_entity_comm.begin() ,
                    m_entity_comm.end() , (Entity*) NULL );

  m_entity_comm.erase( ie , m_entity_comm.end() );
}

//----------------------------------------------------------------------

void BulkData::change_ghosting(
  Ghosting & ghosts ,
  const std::vector<EntityProc> & add_send ,
  const std::vector<Entity*> & remove_receive )
{
  static const char method[] = "stk::mesh::BulkData::change_ghosting" ;

  //----------------------------------------
  // Verify inputs:

  assert_ok_to_modify( method );

  const bool ok_mesh  = & ghosts.mesh() == this ;
  const bool ok_ghost = 1 < ghosts.ordinal();
  bool ok_add    = true ;
  bool ok_remove = true ;

  // Verify all 'add' are locally owned.

  for ( std::vector<EntityProc>::const_iterator
        i = add_send.begin() ; ok_add && i != add_send.end() ; ++i ) {
    ok_add = i->first->owner_rank() == parallel_rank();
  }

  // Verify all 'remove' are members of the ghosting.

  for ( std::vector<Entity*>::const_iterator
        i = remove_receive.begin() ;
        ok_remove && i != remove_receive.end() ; ++i ) {
    ok_remove = in_receive_ghost( ghosts , **i );
  }

  int ok = ok_mesh && ok_ghost && ok_add && ok_remove ;

  all_reduce( parallel() , ReduceMin<1>( & ok ) );

  if ( 0 == ok ) {
    std::ostringstream msg ;
    msg << method << "( " << ghosts.name() << " ) ERROR" ;
    if ( ! ok_mesh )  { msg << " : Mesh does not own this ghosting" ; }
    if ( ! ok_ghost ) { msg << " : Cannot modify this ghosting" ; }
    if ( ! ok_add ) {
      msg << " : Not owned add {" ;
      for ( std::vector<EntityProc>::const_iterator
            i = add_send.begin() ; i != add_send.end() ; ++i ) {
        if ( i->first->owner_rank() != parallel_rank() ) {
          msg << " " ;
          print_entity_key( msg , mesh_meta_data() , i->first->key() );
        }
      }
      msg << " }" ;
    }
    if ( ! ok_remove ) {
      msg << " : Not in ghost receive {" ;
      for ( std::vector<Entity*>::const_iterator
            i = remove_receive.begin() ; i != remove_receive.end() ; ++i ) {
        if ( ! in_receive_ghost( ghosts , **i ) ) {
          msg << " " ;
          print_entity_key( msg , mesh_meta_data() , (*i)->key() );
        }
      }
    }
    throw std::runtime_error( msg.str() );
  }
  //----------------------------------------
  // Change the ghosting:

  internal_change_ghosting( ghosts , add_send , remove_receive );
}

//----------------------------------------------------------------------

namespace {

void require_destroy_entity( BulkData & mesh , Entity * entity ,
                             const char * const method )
{
  if ( ! mesh.destroy_entity( entity ) ) {
    std::ostringstream msg ;
    msg << method << " FAILED attempt to destroy " ;
    print_entity_key( msg , mesh.mesh_meta_data() , entity->key() );
    throw std::logic_error( msg.str() );
  }
}

}

void BulkData::internal_change_ghosting(
  Ghosting & ghosts ,
  const std::vector<EntityProc> & add_send ,
  const std::vector<Entity*> & remove_receive )
{
  const char method[] = "stk::mesh::BulkData::internal_change_ghosting" ;

  const MetaData & meta = m_mesh_meta_data ;
  const unsigned rank_count = meta.entity_rank_count();
  const unsigned p_size = m_parallel_size ;

  //------------------------------------
  // Copy ghosting lists into more efficiently editted container.
  // The send and receive lists must be in entity rank-order.

  std::set< EntityProc , EntityLess > new_send ;
  std::set< Entity * ,   EntityLess > new_recv ;

  //------------------------------------
  // Insert the current ghost receives and then remove from that list.

  if ( & m_entity_comm != & remove_receive ) {

    for ( std::vector<Entity*>::const_iterator
          i = entity_comm().begin() ; i != entity_comm().end() ; ++i ) {
      Entity * const entity = *i ;
      if ( in_receive_ghost( ghosts , *entity ) ) {
        new_recv.insert( entity );
      }
    }

    // Remove any entities that are in the remove list.

    for ( std::vector< Entity * >::const_iterator
          i = remove_receive.begin() ; i != remove_receive.end() ; ++i ) {
      new_recv.erase( *i );
    }

    //  Keep the closure of the remaining received ghosts.
    //  Working from highest-to-lowest key (rank entity type)
    //  results in insertion of the transitive closure.
    //  Insertion will not invalidate the associative container's iterator.

    for ( std::set< Entity * , EntityLess >::iterator
          i = new_recv.end() ; i != new_recv.begin() ; ) {
      --i ;

      const unsigned erank = (*i)->entity_rank();

      for ( PairIterRelation
            irel = (*i)->relations(); ! irel.empty() ; ++irel ) {
        if ( irel->entity_rank() < erank &&
             in_receive_ghost( ghosts , * irel->entity() ) ) {
          new_recv.insert( irel->entity() );
        }
      }
    }
  }

  //  Initialize the new_send from the new_recv
  comm_recv_to_send( *this , new_recv , new_send );

  //------------------------------------
  // Add the specified entities and their closure to the send ghosting

  for ( std::vector< EntityProc >::const_iterator
        i = add_send.begin() ; i != add_send.end() ; ++i ) {
    insert_transitive_closure( new_send , *i );
  }

  // Synchronize the send and receive list.
  // If the send list contains a not-owned entity
  // inform the owner and receiver to ad that entity
  // to their ghost send and receive lists.

  comm_sync_send_recv( *this , new_send , new_recv );

  // The new_send list is now parallel complete and parallel accurate
  // The new_recv has those ghost entities that are to be kept.
  //------------------------------------
  // Remove the ghost entities that will not remain.
  // If the last reference to the receive ghost entity then delete it.

  bool removed = false ;

  for ( std::vector<Entity*>::iterator
        i = m_entity_comm.end() ; i != m_entity_comm.begin() ; ) {

    Entity * entity = *--i ;

    const bool is_owner = entity->owner_rank() == m_parallel_rank ;

    bool remove_recv = false ;

    std::vector< EntityCommInfo >::iterator j = entity->m_comm.end();

    while ( j != entity->m_comm.begin() ) {
      --j ;
      if ( j->ghost_id == ghosts.ordinal() ) {

        remove_recv = ( ! is_owner ) && 0 == new_recv.count( entity );

        const bool remove_send =
          is_owner && 0 == new_send.count( EntityProc( entity , j->proc ) );

        if ( remove_recv || remove_send ) {
          j = entity->m_comm.erase( j );
        }
      }
    }

    if ( entity->m_comm.empty() ) {
      removed = true ;
      *i = NULL ; // No longer communicated
      if ( remove_recv ) {
        require_destroy_entity( *this , entity , method );
      }
    }
  }

  if ( removed ) {
    std::vector<Entity*>::iterator i =
      std::remove( m_entity_comm.begin() ,
                   m_entity_comm.end() , (Entity*) NULL );
    m_entity_comm.erase( i , m_entity_comm.end() );
  }

  //------------------------------------
  // Push newly ghosted entities to the receivers and update the comm list.
  // Unpacking must proceed in entity-rank order so that higher ranking
  // entities that have relations to lower ranking entities will have
  // the lower ranking entities unpacked first.  The higher and lower
  // ranking entities may be owned by different processes,
  // as such unpacking must be performed in rank order.

  {
    const size_t entity_comm_size = m_entity_comm.size();

    CommAll comm( m_parallel_machine );

    for ( std::set< EntityProc , EntityLess >::iterator
          j = new_send.begin(); j != new_send.end() ; ++j ) {

      Entity & entity = * j->first ;

      if ( ! in_ghost( ghosts , entity , j->second ) ) {
        // Not already being sent , must send it.
        CommBuffer & buf = comm.send_buffer( j->second );
        buf.pack<unsigned>( entity.entity_rank() );
        pack_entity_info(  buf , entity );
        pack_field_values( buf , entity );
      }
    }

    comm.allocate_buffers( p_size / 4 );

    for ( std::set< EntityProc , EntityLess >::iterator
          j = new_send.begin(); j != new_send.end() ; ++j ) {

      Entity & entity = * j->first ;

      if ( ! in_ghost( ghosts , entity , j->second ) ) {
        // Not already being sent , must send it.
        CommBuffer & buf = comm.send_buffer( j->second );
        buf.pack<unsigned>( entity.entity_rank() );
        pack_entity_info(  buf , entity );
        pack_field_values( buf , entity );

        entity.insert( EntityCommInfo(ghosts.ordinal(),j->second) );

        m_entity_comm.push_back( & entity );
      }
    }

    comm.communicate();

    std::ostringstream error_msg ;
    int error_count = 0 ;

    for ( unsigned rank = 0 ; rank < rank_count ; ++rank ) {
      for ( unsigned p = 0 ; p < p_size ; ++p ) {
        CommBuffer & buf = comm.recv_buffer(p);
        while ( buf.remaining() ) {
          // Only unpack if of the current entity rank.
          // If not the current entity rank, break the iteration
          // until a subsequent entity rank iteration.
          {
            unsigned this_rank = ~0u ;
            buf.peek<unsigned>( this_rank );

            if ( this_rank != rank ) break ;

            buf.unpack<unsigned>( this_rank );
          }

          PartVector parts ;
          std::vector<Relation> relations ;
          EntityKey key ;
          unsigned  owner = ~0u ;

          unpack_entity_info( buf, *this, key, owner, parts, relations );

          // Must not have the locally_owned_part or locally_used_part

          remove( parts , meta.locally_owned_part() );
          remove( parts , meta.locally_used_part() );

          std::pair<Entity*,bool> result = internal_create_entity( key );

          if ( result.second ) { result.first->m_owner_rank = owner ; }

          assert_entity_owner( method , * result.first , owner );

          internal_change_entity_parts( * result.first , parts , PartVector() );

          declare_relation( * result.first , relations );

          if ( ! unpack_field_values( buf , * result.first , error_msg ) ) {
            ++error_count ;
          }

          const EntityCommInfo tmp( ghosts.ordinal() , owner );

          if ( result.first->insert( tmp ) ) {
            m_entity_comm.push_back( result.first );
          }
        }
      }
    }

    all_reduce( m_parallel_machine , ReduceSum<1>( & error_count ) );

    if ( error_count ) { throw std::runtime_error( error_msg.str() ); }

    if ( entity_comm_size < m_entity_comm.size() ) {
      // Added new ghosting entities to the list,
      // must now sort and merge.

      std::vector<Entity*>::iterator i = m_entity_comm.begin();
      i += entity_comm_size ;
      std::sort( i , m_entity_comm.end() , EntityLess() );
      std::inplace_merge( m_entity_comm.begin() , i ,
                          m_entity_comm.end() , EntityLess() );
      i = std::unique( m_entity_comm.begin() , m_entity_comm.end() );
      m_entity_comm.erase( i , m_entity_comm.end() );
    }
  }

  ghosts.m_sync_count = m_sync_count ;
}

//----------------------------------------------------------------------

namespace {

void insert_transitive_closure( std::set<EntityProc,EntityLess> & new_send ,
                                const EntityProc & entry )
{
  // Do not insert if I can determine that this entity is already
  // owned or shared by the receiving processor.

  if ( entry.second != entry.first->owner_rank() &&
       ! in_shared( * entry.first , entry.second ) ) {

    std::pair< std::set<EntityProc,EntityLess>::iterator , bool >
      result = new_send.insert( entry );

    if ( result.second ) {
      // A new insertion, must also insert the closure

      const unsigned etype = entry.first->entity_rank();
      PairIterRelation irel  = entry.first->relations();

      for ( ; ! irel.empty() ; ++irel ) {
        if ( irel->entity_rank() < etype ) {
          EntityProc tmp( irel->entity() , entry.second );
          insert_transitive_closure( new_send , tmp );
        }
      }
    }
  }
}

// Fill a new send list from the receive list.

void comm_recv_to_send(
  BulkData & mesh ,
  const std::set< Entity * , EntityLess > & new_recv ,
        std::set< EntityProc , EntityLess > & new_send )
{
  static const char method[] = "stk::mesh::BulkData::change_ghosting" ;

  const unsigned parallel_size = mesh.parallel_size();

  CommAll all( mesh.parallel() );

  for ( std::set< Entity * , EntityLess >::const_iterator
        i = new_recv.begin() ; i != new_recv.end() ; ++i ) {
    const unsigned owner = (*i)->owner_rank();
    all.send_buffer( owner ).skip<EntityKey>(1);
  }

  all.allocate_buffers( parallel_size / 4 , false /* Not symmetric */ );

  for ( std::set< Entity * , EntityLess >::const_iterator
        i = new_recv.begin() ; i != new_recv.end() ; ++i ) {
    const unsigned owner = (*i)->owner_rank();
    const EntityKey key = (*i)->key();
    all.send_buffer( owner ).pack<EntityKey>( & key , 1 );
  }

  all.communicate();

  for ( unsigned p = 0 ; p < parallel_size ; ++p ) {
    CommBuffer & buf = all.recv_buffer(p);
    while ( buf.remaining() ) {
      EntityKey key ;
      buf.unpack<EntityKey>( & key , 1 );
      EntityProc tmp( mesh.get_entity( entity_rank(key), entity_id(key) , method ) , p );
      new_send.insert( tmp );
    }
  }
}

// Synchronize the send list to the receive list.

void comm_sync_send_recv(
  BulkData & mesh ,
  std::set< EntityProc , EntityLess > & new_send ,
  std::set< Entity * , EntityLess > & new_recv )
{
  static const char method[] = "stk::mesh::BulkData::change_ghosting" ;
  const unsigned parallel_rank = mesh.parallel_rank();
  const unsigned parallel_size = mesh.parallel_size();

  CommAll all( mesh.parallel() );

  // Communication sizing:

  for ( std::set< EntityProc , EntityLess >::iterator
        i = new_send.begin() ; i != new_send.end() ; ++i ) {
    const unsigned owner = i->first->owner_rank();
    all.send_buffer( i->second ).skip<EntityKey>(2);
    if ( owner != parallel_rank ) {
      all.send_buffer( owner ).skip<EntityKey>(2);
    }
  }

  all.allocate_buffers( parallel_size / 4 , false /* Not symmetric */ );

  // Communication packing (with message content comments):
  for ( std::set< EntityProc , EntityLess >::iterator
        i = new_send.begin() ; i != new_send.end() ; ) {
    const unsigned owner = i->first->owner_rank();

    // Inform receiver of ghosting, the receiver does not own
    // and does not share this entity.
    // The ghost either already exists or is a to-be-done new ghost.
    // This status will be resolved on the final communication pass
    // when new ghosts are packed and sent.

    const EntityKey &entity_key = i->first->key();
    const uint64_t &proc = i->second;

    all.send_buffer( i->second ).pack(entity_key).pack(proc);

    if ( owner != parallel_rank ) {
      // I am not the owner of this entity.
      // Inform the owner of this ghosting need.
      all.send_buffer( owner ).pack(entity_key).pack(proc);

      // Erase it from my processor's ghosting responsibility:
      // The iterator passed to the erase method will be invalidated.
      std::set< EntityProc , EntityLess >::iterator jrem = i ; ++i ;
      new_send.erase( jrem );
    }
    else {
      ++i ;
    }
  }

  all.communicate();

  // Communication unpacking:
  for ( unsigned p = 0 ; p < parallel_size ; ++p ) {
    CommBuffer & buf = all.recv_buffer(p);
    while ( buf.remaining() ) {

      EntityKey entity_key;
      uint64_t proc(0);

      buf.unpack(entity_key).unpack(proc);

      Entity * const e = mesh.get_entity( entity_key );

      if ( parallel_rank != proc ) {
        //  Receiving a ghosting need for an entity I own.
        //  Add it to my send list.
        if ( e == NULL ) {
          throw std::logic_error( std::string(method) );
        }
        EntityProc tmp( e , proc );
        new_send.insert( tmp );
      }
      else if ( e != NULL ) {
        //  I am the receiver for this ghost.
        //  If I already have it add it to the receive list,
        //  otherwise don't worry about it - I will receive
        //  it in the final new-ghosting communication.
        new_recv.insert( e );
      }
    }
  }
}

} // namespace <>

//----------------------------------------------------------------------
//----------------------------------------------------------------------

void BulkData::internal_regenerate_shared_aura()
{
  static const char method[] =
    "stk::mesh::BulkData::internal_regenerate_shared_aura" ;

  assert_ok_to_modify( method );

  std::vector<EntityProc> send ;

  for ( std::vector<Entity*>::const_iterator
        i = entity_comm().begin() ; i != entity_comm().end() ; ++i ) {

    Entity & entity = **i ;

    const unsigned erank = entity.entity_rank();

    const PairIterEntityComm sharing = entity.sharing();

    for ( size_t j = 0 ; j < sharing.size() ; ++j ) {

      const unsigned p = sharing[j].proc ;

      for ( PairIterRelation
            rel = entity.relations() ; ! rel.empty() ; ++rel ) {

        Entity * const rel_entity = rel->entity();

        // Higher rank and I own it, ghost to the sharing processor
        if ( erank < rel_entity->entity_rank() &&
                     rel_entity->owner_rank() == m_parallel_rank &&
             ! in_shared( *rel_entity , p ) ) {

          EntityProc entry( rel_entity , p );
          send.push_back( entry );
        }
      }
    }
  }

  // Add new aura, remove all of the old aura.
  // The change_ghosting figures out what to actually delete and add.

  internal_change_ghosting( shared_aura() , send , m_entity_comm );
}

//----------------------------------------------------------------------
//----------------------------------------------------------------------

} // namespace mesh
} // namespace stk

