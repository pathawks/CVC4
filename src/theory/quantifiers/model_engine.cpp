/*********************                                                        */
/*! \file model_engine.cpp
 ** \verbatim
 ** Top contributors (to current version):
 **   Andrew Reynolds, Morgan Deters, Tim King
 ** This file is part of the CVC4 project.
 ** Copyright (c) 2009-2017 by the authors listed in the file AUTHORS
 ** in the top-level source directory) and their institutional affiliations.
 ** All rights reserved.  See the file COPYING in the top-level source
 ** directory for licensing information.\endverbatim
 **
 ** \brief Implementation of model engine class
 **/

#include "theory/quantifiers/model_engine.h"

#include "options/quantifiers_options.h"
#include "theory/quantifiers/ambqi_builder.h"
#include "theory/quantifiers/first_order_model.h"
#include "theory/quantifiers/full_model_check.h"
#include "theory/quantifiers/quantifiers_attributes.h"
#include "theory/quantifiers/term_database.h"
#include "theory/theory_engine.h"
#include "theory/uf/equality_engine.h"
#include "theory/uf/theory_uf.h"
#include "theory/uf/theory_uf_strong_solver.h"

using namespace std;
using namespace CVC4;
using namespace CVC4::kind;
using namespace CVC4::context;
using namespace CVC4::theory;
using namespace CVC4::theory::quantifiers;
using namespace CVC4::theory::inst;

//Model Engine constructor
ModelEngine::ModelEngine( context::Context* c, QuantifiersEngine* qe ) :
QuantifiersModule( qe ),
d_incomplete_check(true),
d_addedLemmas(0),
d_triedLemmas(0),
d_totalLemmas(0)
{

}

ModelEngine::~ModelEngine() {

}

bool ModelEngine::needsCheck( Theory::Effort e ) {
  return e==Theory::EFFORT_LAST_CALL;
}

unsigned ModelEngine::needsModel( Theory::Effort e ) {
  return QuantifiersEngine::QEFFORT_MODEL;
}

void ModelEngine::reset_round( Theory::Effort e ) {
  d_incomplete_check = true;
}

void ModelEngine::check( Theory::Effort e, unsigned quant_e ){
  if( quant_e==QuantifiersEngine::QEFFORT_MODEL ){
    Assert( !d_quantEngine->inConflict() );
    int addedLemmas = 0;
    FirstOrderModel* fm = d_quantEngine->getModel();

    //the following will test that the model satisfies all asserted universal quantifiers by
    // (model-based) exhaustive instantiation.
    double clSet = 0;
    if( Trace.isOn("model-engine") ){
      Trace("model-engine") << "---Model Engine Round---" << std::endl;
      clSet = double(clock())/double(CLOCKS_PER_SEC);
    }

    Trace("model-engine-debug") << "Verify uf ss is minimal..." << std::endl;
    //let the strong solver verify that the model is minimal
    //for debugging, this will if there are terms in the model that the strong solver was not notified of
    uf::StrongSolverTheoryUF * ufss = ((uf::TheoryUF*)d_quantEngine->getTheoryEngine()->theoryOf( THEORY_UF ))->getStrongSolver();
    if( !ufss || ufss->debugModel( fm ) ){
      Trace("model-engine-debug") << "Check model..." << std::endl;
      d_incomplete_check = false;
      //print debug
      if( Trace.isOn("fmf-model-complete") ){
        Trace("fmf-model-complete") << std::endl;
        debugPrint("fmf-model-complete");
      }
      //successfully built an acceptable model, now check it
      addedLemmas += checkModel();
    }else{
      addedLemmas++;
    }

    if( Trace.isOn("model-engine") ){
      double clSet2 = double(clock())/double(CLOCKS_PER_SEC);
      Trace("model-engine") << "Finished model engine, time = " << (clSet2-clSet) << std::endl;
    }

    if( addedLemmas==0 ){
      Trace("model-engine-debug") << "No lemmas added, incomplete = " << ( d_incomplete_check || !d_incomplete_quants.empty() ) << std::endl;
      //CVC4 will answer SAT or unknown
      if( Trace.isOn("fmf-consistent") ){
        Trace("fmf-consistent") << std::endl;
        debugPrint("fmf-consistent");
      }
    }
  }
}

bool ModelEngine::checkComplete() {
  return !d_incomplete_check;
}

bool ModelEngine::checkCompleteFor( Node q ) {
  return std::find( d_incomplete_quants.begin(), d_incomplete_quants.end(), q )==d_incomplete_quants.end();
}

void ModelEngine::registerQuantifier( Node f ){
  if( Trace.isOn("fmf-warn") ){
    bool canHandle = true;
    for( unsigned i=0; i<f[0].getNumChildren(); i++ ){
      TypeNode tn = f[0][i].getType();
      if( !tn.isSort() ){
        if( !tn.getCardinality().isFinite() ){
          if( tn.isInteger() ){
            if( !options::fmfBound() ){
              canHandle = false;
            }
          }else{
            canHandle = false;
          }
        }
      }
    }
    if( !canHandle ){
      Trace("fmf-warn") << "Warning : Model Engine : may not be able to answer SAT because of formula : " << f << std::endl;
    }
  }
}

void ModelEngine::assertNode( Node f ){

}

bool ModelEngine::optOneQuantPerRound(){
  return options::fmfOneQuantPerRound();
}


int ModelEngine::checkModel(){
  FirstOrderModel* fm = d_quantEngine->getModel();

  //flatten the representatives
  //Trace("model-engine-debug") << "Flattening representatives...." << std::endl;
  //d_quantEngine->getEqualityQuery()->flattenRepresentatives( fm->d_rep_set.d_type_reps );

  //for debugging, setup
  for( std::map< TypeNode, std::vector< Node > >::iterator it = fm->d_rep_set.d_type_reps.begin();
       it != fm->d_rep_set.d_type_reps.end(); ++it ){
    if( it->first.isSort() ){
      Trace("model-engine") << "Cardinality( " << it->first << " )" << " = " << it->second.size() << std::endl;
      Trace("model-engine-debug") << "        Reps : ";
      for( size_t i=0; i<it->second.size(); i++ ){
        Trace("model-engine-debug") << it->second[i] << "  ";
      }
      Trace("model-engine-debug") << std::endl;
      Trace("model-engine-debug") << "   Term reps : ";
      for( size_t i=0; i<it->second.size(); i++ ){
        Node r = d_quantEngine->getInternalRepresentative( it->second[i], Node::null(), 0 );
        Trace("model-engine-debug") << r << " ";
      }
      Trace("model-engine-debug") << std::endl;
      Node mbt = d_quantEngine->getTermDatabase()->getModelBasisTerm(it->first);
      Trace("model-engine-debug") << "  Basis term : " << mbt << std::endl;
    }
  }

  d_triedLemmas = 0;
  d_addedLemmas = 0;
  d_totalLemmas = 0;
  //for statistics
  if( Trace.isOn("model-engine") ){
    for( unsigned i=0; i<fm->getNumAssertedQuantifiers(); i++ ){
      Node f = fm->getAssertedQuantifier( i );
      if( d_quantEngine->getModel()->isQuantifierActive( f ) && d_quantEngine->hasOwnership( f, this ) ){
        int totalInst = 1;
        for( unsigned j=0; j<f[0].getNumChildren(); j++ ){
          TypeNode tn = f[0][j].getType();
          if( fm->d_rep_set.hasType( tn ) ){
            totalInst = totalInst * (int)fm->d_rep_set.d_type_reps[ tn ].size();
          }
        }
        d_totalLemmas += totalInst;
      }
    }
  }

  Trace("model-engine-debug") << "Do exhaustive instantiation..." << std::endl;
  // FMC uses two sub-effort levels
  int e_max = options::mbqiMode()==MBQI_FMC || options::mbqiMode()==MBQI_FMC_INTERVAL ? 2 : ( options::mbqiMode()==MBQI_TRUST ? 0 : 1 );
  for( int e=0; e<e_max; e++) {
    d_incomplete_quants.clear();
    for( unsigned i=0; i<fm->getNumAssertedQuantifiers(); i++ ){
      Node q = fm->getAssertedQuantifier( i, true );
      Trace("fmf-exh-inst") << "-> Exhaustive instantiate " << q << ", effort = " << e << "..." << std::endl;
      //determine if we should check this quantifier
      if( d_quantEngine->getModel()->isQuantifierActive( q ) && d_quantEngine->hasOwnership( q, this ) ){
        exhaustiveInstantiate( q, e );
        if( d_quantEngine->inConflict() || ( optOneQuantPerRound() && d_addedLemmas>0 ) ){
          break;
        }
      }else{
        Trace("fmf-exh-inst") << "-> Inactive : " << q << std::endl;
      }
    }
    if( d_addedLemmas>0 ){
      break;
    }else{
      Assert( !d_quantEngine->inConflict() );
    }
  }

  //print debug information
  if( d_quantEngine->inConflict() ){
    Trace("model-engine") << "Conflict, added lemmas = ";
  }else{
    Trace("model-engine") << "Added Lemmas = ";
  } 
  Trace("model-engine") << d_addedLemmas << " / " << d_triedLemmas << " / ";
  Trace("model-engine") << d_totalLemmas << std::endl;
  return d_addedLemmas;
}



void ModelEngine::exhaustiveInstantiate( Node f, int effort ){
  //first check if the builder can do the exhaustive instantiation
  quantifiers::QModelBuilder * mb = d_quantEngine->getModelBuilder();
  unsigned prev_alem = mb->getNumAddedLemmas();
  unsigned prev_tlem = mb->getNumTriedLemmas();
  int retEi = mb->doExhaustiveInstantiation( d_quantEngine->getModel(), f, effort );
  if( retEi!=0 ){
    if( retEi<0 ){
      Trace("fmf-exh-inst") << "-> Builder determined complete instantiation was impossible." << std::endl;
      d_incomplete_quants.push_back( f );
    }else{
      Trace("fmf-exh-inst") << "-> Builder determined instantiation(s)." << std::endl;
    }
    d_triedLemmas += mb->getNumTriedLemmas()-prev_tlem;
    d_addedLemmas += mb->getNumAddedLemmas()-prev_alem;
    d_quantEngine->d_statistics.d_instantiations_fmf_mbqi += mb->getNumAddedLemmas();
  }else{
    if( Trace.isOn("fmf-exh-inst-debug") ){
      Trace("fmf-exh-inst-debug") << "   Instantiation Constants: ";
      for( size_t i=0; i<f[0].getNumChildren(); i++ ){
        Trace("fmf-exh-inst-debug") << d_quantEngine->getTermDatabase()->getInstantiationConstant( f, i ) << " ";
      }
      Trace("fmf-exh-inst-debug") << std::endl;
    }
    //create a rep set iterator and iterate over the (relevant) domain of the quantifier
    RepSetIterator riter( d_quantEngine, &(d_quantEngine->getModel()->d_rep_set) );
    if( riter.setQuantifier( f ) ){
      Trace("fmf-exh-inst") << "...exhaustive instantiation set, incomplete=" << riter.isIncomplete() << "..." << std::endl;
      if( !riter.isIncomplete() ){
        int triedLemmas = 0;
        int addedLemmas = 0;
        while( !riter.isFinished() && ( addedLemmas==0 || !options::fmfOneInstPerRound() ) ){
          //instantiation was not shown to be true, construct the match
          InstMatch m( f );
          for( int i=0; i<riter.getNumTerms(); i++ ){
            m.set( d_quantEngine, i, riter.getCurrentTerm( i ) );
          }
          Debug("fmf-model-eval") << "* Add instantiation " << m << std::endl;
          triedLemmas++;
          //add as instantiation
          if( d_quantEngine->addInstantiation( f, m, true ) ){
            addedLemmas++;
            if( d_quantEngine->inConflict() ){
              break;
            }
          }else{
            Debug("fmf-model-eval") << "* Failed Add instantiation " << m << std::endl;
          }
          riter.increment();
        }
        d_addedLemmas += addedLemmas;
        d_triedLemmas += triedLemmas;
        d_quantEngine->d_statistics.d_instantiations_fmf_exh += addedLemmas;
      }
    }else{
      Trace("fmf-exh-inst") << "...exhaustive instantiation did set, incomplete=" << riter.isIncomplete() << "..." << std::endl;
    }
    //if the iterator is incomplete, we will return unknown instead of sat if no instantiations are added this round
    if( riter.isIncomplete() ){
      d_incomplete_quants.push_back( f );
    }
  }
}

void ModelEngine::debugPrint( const char* c ){
  Trace( c ) << "Quantifiers: " << std::endl;
  for( unsigned i=0; i<d_quantEngine->getModel()->getNumAssertedQuantifiers(); i++ ){
    Node q = d_quantEngine->getModel()->getAssertedQuantifier( i );
    if( d_quantEngine->hasOwnership( q, this ) ){
      Trace( c ) << "   ";
      if( !d_quantEngine->getModel()->isQuantifierActive( q ) ){
        Trace( c ) << "*Inactive* ";
      }else{
        Trace( c ) << "           ";
      }
      Trace( c ) << q << std::endl;
    }
  }
  //d_quantEngine->getModel()->debugPrint( c );
}

