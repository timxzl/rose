
#include "sage3basic.h"
#include "sageBuilder.h"
#include "Outliner.hh"

#include "nanos_ompss.h"
#include "omp_lowering.h"

using namespace std;
using namespace SageInterface;
using namespace SageBuilder;

namespace OmpSupport {
namespace NanosLowering {

//! Create a function named 'func_name_str' containing an OpenMP loop to be executed with Nanos
// (Inspired in generateFunction method)
// For an input 's' such as:
//    int i;
//    for (i = 0; i <= 9; i += 1) {
//        a[i] = (i * 2);
//    }
// The outlined function body 'func_body' will be:
//    nanos_ws_item_loop_t nanos_item_loop;
//    err = nanos_worksharing_next_item( data->wsd, ( void ** ) &nanos_item_loop );
//    if( err != 0 /*NANOS_OK*/ )
//        nanos_handle_error( err );
//    int _p_i;
//    while( nanos_item_loop.execute ) {
//        for( _p_i = nanos_item_loop.lower; _p_i <= nanos_item_loop.upper; ++_p_i ) {
//            a[_p_i] = (_p_i * 2);
//        }
//        err = nanos_worksharing_next_item( data->wsd, ( void ** ) &nanos_item_loop );
//    }
static SgFunctionDeclaration * generateLoopFunction( 
        SgBasicBlock* s, const string& func_name_str,
        ASTtools::VarSymSet_t& syms, ASTtools::VarSymSet_t& pdSyms,
        SgClassDeclaration* struct_decl, SgScopeStatement* scope,
        std::set<SgVariableDeclaration *>& unpacking_stmts )
{
    ROSE_ASSERT( s && scope );
    ROSE_ASSERT( isSgGlobal( scope ) );
    
    //step 1. Create function skeleton, 'func'.
    //---------------------------------------------------------------------------------------
    SgName func_name( func_name_str );
    SgFunctionParameterList* parameterList = buildFunctionParameterList( );
    SgFunctionDeclaration* func = Outliner::createFuncSkeleton( func_name, buildVoidType( ), parameterList, scope );
    ROSE_ASSERT( func );
    
    if( SageInterface::is_Cxx_language( ) || is_mixed_C_and_Cxx_language( ) )
    { // Make function 'extern "C"'
        func->get_declarationModifier( ).get_storageModifier( ).setExtern( );
        func->set_linkage( "C" );
    }

    //step 2.1 Create the function body Part 1
    //---------------------------------------------------------------------------------------
    // This process is divided in two parts
    // 1.- Copy the statements in the original 'source' to the new 'func_body'
    // 2.- Generate the nanos loop (threads) that will iterate over the actual loop
    // We need to split this proces because in between we call 'variableHandling' 
    // to generate the parameter struct, introduce provate variables, etc.
    // The private variables are added just before the ForStatement, so if we don't split the proces,
    // they will be inside the nanos while loop, and we need them outside this loop.
    // Here we only perform step 1: copying the original statements into the new 'func_body'
    SgBasicBlock* func_body = func->get_definition( )->get_body( );
    ROSE_ASSERT( func_body != NULL );
    ROSE_ASSERT( func_body->get_statements( ).empty( ) == true );
    SageInterface::moveStatementsBetweenBlocks( s, func_body );

    //step 3: variable handling: create parameters, packing and unpacking statements and replace variables
    //---------------------------------------------------------------------------------------
    // Source:                                      Target:
    //     int i;                                       int _p_i;
    //     for (i = 0; i <= 9; i += 1)                  for (_p_i = 0; _p_i <= 9; _p_i += 1)
    //         a[i] = (i * 2);                              a[_p_i] = (_p_i * 2);
    std::set<SgInitializedName *> restoreVars;
    unpacking_stmts = Outliner::variableHandling( syms, pdSyms, restoreVars, struct_decl, func );
    ROSE_ASSERT( func != NULL );

    if( Outliner::useNewFile )
        ASTtools::setSourcePositionAtRootAndAllChildrenAsTransformation( func_body );

    //step 2.2 Create the function body Part 2
    //---------------------------------------------------------------------------------------
    // Here we wrap the original loop into the nanos while iteration over the threads

    // We need an extra parameter with the worksharing information
    SgInitializedName * wsd_param = buildInitializedName( "wsd", buildPointerType( buildOpaqueType( "nanos_ws_desc_t", func_body ) ) );

    // Create the Nanos worksharing that will iterate over the sections
    //     nanos_ws_item_loop_t nanos_item_loop;
    //     err = nanos_worksharing_next_item( data->wsd, ( void ** ) &nanos_item_loop );
    //     if( err != 0 /*NANOS_OK*/ )
    //         nanos_handle_error( err );
    SgType * nanos_item_loop_type = buildOpaqueType( "nanos_ws_item_loop_t", func_body );   // We do not create the type because we dont want it to cast to 'struct'
    SgVariableDeclaration* nanos_item_loop = buildVariableDeclaration( "nanos_item_loop", nanos_item_loop_type, NULL, func_body );
    SgExprListExp * nanos_next_item_params = buildExprListExp( buildVarRefExp( wsd_param, func_body ), 
                                                               buildCastExp( buildAddressOfOp( buildVarRefExp( nanos_item_loop ) ),
                                                                             buildPointerType( buildPointerType( buildVoidType( ) ) ) ) );
    SgType * nanos_err_type = buildOpaqueType( "nanos_err_t", func_body );
    SgFunctionCallExp * nanos_next_item_call = buildFunctionCallExp( "nanos_worksharing_next_item", nanos_err_type, nanos_next_item_params, func_body );
    SgInitializer * nanos_next_item_init = buildAssignInitializer( nanos_next_item_call, nanos_err_type );
    SgVariableDeclaration * nanos_next_item_error = buildVariableDeclaration( "nanos_err", nanos_err_type, nanos_next_item_init, func_body );
    SgStatement * nanos_handle_error = buildFunctionCallStmt( "nanos_handle_error", buildVoidType( ), 
                                                              buildExprListExp( buildVarRefExp( nanos_next_item_error ) ), func_body );
    SgStatement * nanos_next_item_check_result = buildIfStmt( buildNotEqualOp( buildVarRefExp( nanos_next_item_error ), buildIntVal( 0 ) ),
                                                              nanos_handle_error, NULL );
    prependStatement( nanos_next_item_check_result, func_body );
    prependStatement( nanos_next_item_error, func_body );
    prependStatement( nanos_item_loop, func_body );

    // Nanos worksharing iteraton
    //    int _p_i;
    //    while( nanos_item_loop.execute ) {
    //        for( _p_i = nanos_item_loop.lower; _p_i <= nanos_item_loop.upper; ++_p_i ) {
    //            ...
    //        }
    //        err = nanos_worksharing_next_item( data->wsd, ( void ** ) &nanos_item_loop );
    //    }
    SgStatement * while_cond =  buildExprStatement( buildDotExp( buildVarRefExp( nanos_item_loop ), 
                                                                 buildOpaqueVarRefExp( "execute", func_body ) ) );
    SgBasicBlock * while_body = buildBasicBlock( );

    Rose_STL_Container<SgNode *> for_list = NodeQuery::querySubTree( func_body, V_SgForStatement );
    ROSE_ASSERT( !for_list.empty( ) );
    SgStatement * source_loop = isSgForStatement( for_list[0] );
    SgForStatement * target_loop = isSgForStatement( SageInterface::copyStatement( source_loop ) );
    SgExpression * loop_lower = buildDotExp( buildVarRefExp( nanos_item_loop ), buildOpaqueVarRefExp( "lower", while_body ) );
    SgExpression * loop_upper = buildDotExp( buildVarRefExp( nanos_item_loop ), buildOpaqueVarRefExp( "upper", while_body ) );
    SageInterface::setLoopLowerBound( target_loop, loop_lower );
    SageInterface::setLoopUpperBound( target_loop, loop_upper );
    SageInterface::appendStatement( target_loop, while_body );

    SgStatement * next_item_call = buildFunctionCallStmt( "nanos_worksharing_next_item", nanos_err_type, nanos_next_item_params, func_body );
    while_body->append_statement( next_item_call );
    SgWhileStmt * nanos_while = buildWhileStmt( while_cond, while_body );
    SageInterface::replaceStatement( source_loop, nanos_while );

    //step 4: parameters checking
    //---------------------------------------------------------------------------------------
    // Member 'wsd' is only needed when there is a reduction performed sections construct
    // But we add it always to match the header of the Nanos sections method
    SgFunctionParameterList * params = func->get_parameterList( );
    SageInterface::appendArg( params, wsd_param );
    func->set_type( buildFunctionType( func->get_type()->get_return_type( ), 
                    buildFunctionParameterTypeList( params ) ) );

    // Retest this...
    ROSE_ASSERT( func_body->get_parent( ) == func->get_definition( ) );
    ROSE_ASSERT( scope->lookup_function_symbol( func->get_name( ) ) );

    return func;
}

//! A helper function to generate explicit function for omp loop 
//! Inspired in method 'generateOutlinedTask'
SgFunctionDeclaration* generateOutlinedLoop( 
        SgOmpForStatement * source, std::string & wrapper_name, 
        ASTtools::VarSymSet_t& syms, ASTtools::VarSymSet_t& pdSyms3, 
        SgClassDeclaration * & struct_decl )
{
    // Check the parameters
    ROSE_ASSERT( source != NULL );
    SgForStatement * for_body =  isSgForStatement( source->get_body( ) );
    ROSE_ASSERT( for_body != NULL );
    
    // Initialize outliner 
    Outliner::useParameterWrapper = true; 
    Outliner::useStructureWrapper = true;
    SgBasicBlock* body_block = Outliner::preprocess( for_body );
    
    // Variable handling is done after Outliner::preprocess() to ensure a basic block for the body,
    // but before calling the actual outlining.
    // This simplifies the outlining since firstprivate, private variables are replaced 
    // with their local copies before outliner is used 
    transOmpVariables( source, body_block );
    
    string func_name = Outliner::generateFuncName( source );
    SgGlobal* g_scope = SageInterface::getGlobalScope( body_block );
    ROSE_ASSERT( g_scope != NULL );
    
    // This step is less useful for private, firstprivate, and reduction variables
    // since they are already handled by transOmpVariables(). 
    Outliner::collectVars( body_block, syms );
    
    // We have to add the reduction symbols to the 'syms' list
    // because they have been considered firstprivate during "transOmpVariables"
    // and Nanos avoids the copyBack statement ( it is performed transparently ) 
    // That allows 'collectVars' to take into account these symbols
    ASTtools::VarSymSet_t red_syms;
    SgInitializedNamePtrList red_vars = collectClauseVariables( source, V_SgOmpReductionClause );
    convertAndFilter( red_vars, red_syms );
    set_union( syms.begin( ), syms.end(),
               red_syms.begin( ), red_syms.end( ),
               std::inserter( syms, syms.begin( ) ) );
    
    // Assume all parameters need to be passed by reference/pointers first
    ASTtools::VarSymSet_t pSyms, fpSyms,reductionSyms, pdSyms;
    std::copy( syms.begin( ), syms.end( ), std::inserter( pdSyms, pdSyms.begin( ) ) );
    
    // Exclude firstprivate variables: they are read only in fact
    // TODO keep class typed variables!!!  even if they are firstprivate or private!! 
    SgInitializedNamePtrList fp_vars = collectClauseVariables( source, V_SgOmpFirstprivateClause );
    ASTtools::VarSymSet_t fp_syms, pdSyms2;
    convertAndFilter( fp_vars, fp_syms );
    set_difference( pdSyms.begin( ), pdSyms.end( ),
                    fp_syms.begin( ), fp_syms.end( ),
                    std::inserter( pdSyms2, pdSyms2.begin( ) ) );
    
    // Similarly , exclude private variable, also read only
    SgInitializedNamePtrList p_vars = collectClauseVariables( source, V_SgOmpPrivateClause );
    ASTtools::VarSymSet_t p_syms; //, pdSyms3;
    convertAndFilter( p_vars, p_syms );
    //TODO keep class typed variables!!!  even if they are firstprivate or private!! 
    set_difference( pdSyms2.begin( ), pdSyms2.end( ),
                    p_syms.begin( ), p_syms.end( ),
                    std::inserter( pdSyms3, pdSyms3.begin( ) ) );
    
    // lastprivate and reduction variables cannot be excluded  since write access to their shared copies
    
    // Add sizes of array variables when those are also variables
    ASTtools::VarSymSet_t new_syms;
    for (ASTtools::VarSymSet_t::const_iterator i = syms.begin (); i != syms.end (); ++i)
    {
        SgType* i_type = (*i)->get_declaration()->get_type();
    
        while (isSgArrayType(i_type))
        {
            // Get most significant dimension
            SgExpression* index = ((SgArrayType*) i_type)->get_index();
        
            // Get the variables used to compute the dimension
            // FIXME We insert a new statement and delete it afterwards in order to use "collectVars" function
            //       Think about implementing an specific function for expressions
            ASTtools::VarSymSet_t a_syms, a_pSyms;
            SgExprStatement* index_stmt = buildExprStatement(index);
            appendStatement(index_stmt, body_block);
            Outliner::collectVars(index_stmt, a_syms);
            SageInterface::removeStatement(index_stmt);
            for(ASTtools::VarSymSet_t::iterator j = a_syms.begin(); j != a_syms.end(); ++j)
            {
                const SgVariableSymbol* s = *j;
                new_syms.insert(s);   // If the symbol is not in the symbol list, it is added
            }
            
            // Advance over the type
            i_type = ((SgArrayType*) i_type)->get_base_type();
        }
    }
    for (ASTtools::VarSymSet_t::const_iterator i = new_syms.begin (); i != new_syms.end (); ++i)
    {
        const SgVariableSymbol* s = *i;
        syms.insert(s);
    }
    
    // Data structure used to wrap parameters
    struct_decl = Outliner::generateParameterStructureDeclaration( body_block, func_name, syms, pdSyms3, 
                                                                   g_scope, /*nanos_ws*/ true );
    
    // Generate the outlined function
    std::set<SgVariableDeclaration *> unpacking_stmts;
    SgFunctionDeclaration * result = generateLoopFunction( body_block, func_name, syms, pdSyms3,
                                                           struct_decl, g_scope, unpacking_stmts );
    ROSE_ASSERT( result != NULL );
    
    Outliner::insert( result, g_scope, body_block );
    
    if( result->get_definingDeclaration( ) != NULL )
        SageInterface::setStatic( result->get_definingDeclaration( ) );
    if( result->get_firstNondefiningDeclaration( ) != NULL )
        SageInterface::setStatic( result->get_firstNondefiningDeclaration( ) );

    // We have to check here whether there is a reduction clause
    // Nanos outlines reductions in a different way than other RTLs
    // Whereas gomp and omni do the transformation in transOmpVariables 
    if( hasClause( source, V_SgOmpReductionClause ) )
    {
        generate_nanos_reduction( result, source, struct_decl, func_name, syms, pdSyms3, unpacking_stmts );
    }
    
    ASTtools::VarSymSet_t redution_syms;
    wrapper_name = Outliner::generatePackingStatements( source, syms, pdSyms3, struct_decl, redution_syms );
    
    return result;
}

//! Create a function named 'func_name_str' containing an OpenMP sections to be executed with Nanos
// (Inspired in generateFunction method)
// For an input 's' such as:
//    #pragma omp sections
//        #pragma omp section
//            i = 1;
//        #pragma omp section
//            i = 2
// The outlined function body 'func_body' will be:
//    // typedef struct {
//    //     int   lower;      // loop item lower bound
//    //     int   upper;      // loop item upper bound
//    //     bool  execute;    // is a valid loop item?
//    //     bool  last;       // is the last loop item?
//    // } nanos_ws_item_loop_t;
//    nanos_ws_item_loop_t nanos_item_loop;
//    nanos_err_t nanos_err = nanos_worksharing_next_item(wsd,((void **)(&nanos_item_loop)));
//    if (nanos_err != 0) 
//        nanos_handle_error(nanos_err);
//    while(nanos_item_loop.execute) {
//        int nanos_item_iter;
//        for (nanos_item_iter = nanos_item_loop.lower; nanos_item_iter <= nanos_item_loop.upper; nanos_item_iter++) 
//            switch(nanos_item_iter) {
//                case 0:
//                    _p_i = 1;
//                    break;
//                case 1:
//                    _p_i = 2;
//                    break; 
//            }
//        nanos_worksharing_next_item(wsd,((void **)(&nanos_item_loop)));
//    }
static SgFunctionDeclaration * generateSectionsFunction( 
        SgBasicBlock* s, const std::string& func_name_str,
        const ASTtools::VarSymSet_t& syms, const ASTtools::VarSymSet_t& pdSyms,
        const std::set<SgInitializedName *>& restoreVars,       // last private variables that must be restored
        SgClassDeclaration* struct_decl, SgScopeStatement* scope, 
        std::set<SgVariableDeclaration *>& unpacking_stmts )
{
    ROSE_ASSERT( s && scope );
    ROSE_ASSERT( isSgGlobal( scope ) );
    
    //step 1. Create function skeleton, 'func'.
    //---------------------------------------------------------------------------------------
    SgFunctionParameterList* parameterList = buildFunctionParameterList( );
    SgFunctionDeclaration* func = Outliner::createFuncSkeleton( func_name_str, buildVoidType( ), parameterList, scope );
    ROSE_ASSERT( func );
    if( SageInterface::is_Cxx_language( ) || is_mixed_C_and_Cxx_language( ) )
    {   // Enable C code to call this outlined function making the function 'extern "C"'
        func->get_declarationModifier( ).get_storageModifier( ).setExtern( );
        func->set_linkage( "C" );
    }
    
    //step 2. Create the function body
    //---------------------------------------------------------------------------------------
    SgBasicBlock * func_body = func->get_definition( )->get_body( );
    ROSE_ASSERT( func_body != NULL );
    
    // copy all statements from 's' until we find the sections block: local declarations
    SgStatementPtrList & stmt_list = s->get_statements( );
    SgStatementPtrList::iterator it = stmt_list.begin( );
    while( !isSgOmpSectionStatement( *it ) )
    {
        appendStatement( SageInterface::copyStatement( *it ), func_body );
        ++it;
    }
    ROSE_ASSERT( isSgOmpSectionStatement( *it ) );
    
    // Create the Nanos worksharing that will iterate over the sections
    //     nanos_ws_item_loop_t nanos_item_loop;
    //     err = nanos_worksharing_next_item( wsd, ( void ** ) &nanos_item_loop );
    //     if( err != 0 /*NANOS_OK*/ )
    //         nanos_handle_error( err );
    SgType * nanos_item_loop_type = buildOpaqueType( "nanos_ws_item_loop_t", func_body );   // We do not create the type because we dont want it to cast to 'struct'
    SgVariableDeclaration* nanos_item_loop = buildVariableDeclaration( "nanos_item_loop", nanos_item_loop_type, NULL, func_body );
    SgExprListExp * nanos_next_item_params = buildExprListExp( buildOpaqueVarRefExp( "wsd", func_body ),
                                                               buildCastExp( buildAddressOfOp( buildVarRefExp( nanos_item_loop ) ),
                                                                             buildPointerType( buildPointerType( buildVoidType( ) ) ) ) );
    SgType * nanos_err_type = buildOpaqueType( "nanos_err_t", func_body );
    SgFunctionCallExp * nanos_next_item_call = buildFunctionCallExp( "nanos_worksharing_next_item", nanos_err_type, nanos_next_item_params, func_body );
    SgInitializer * nanos_next_item_init = buildAssignInitializer( nanos_next_item_call, nanos_err_type );
    SgVariableDeclaration * nanos_next_item_error = buildVariableDeclaration( "nanos_err", nanos_err_type, nanos_next_item_init, func_body );
    SgStatement * nanos_handle_error = buildFunctionCallStmt( "nanos_handle_error", buildVoidType( ), 
                                                              buildExprListExp( buildVarRefExp( nanos_next_item_error ) ), func_body );
    SgStatement * nanos_next_item_check_result = buildIfStmt( buildNotEqualOp( buildVarRefExp( nanos_next_item_error ), buildIntVal( 0 ) ),
                                                              nanos_handle_error, NULL );
    appendStatement( nanos_item_loop, func_body );
    appendStatement( nanos_next_item_error, func_body );
    appendStatement( nanos_next_item_check_result, func_body );
    
    // Nanos worksharing iteraton
    //    while( nanos_item_loop.execute ) {
    //        int i;
    //        for( i = nanos_item_loop.lower; i <= nanos_item_loop.upper; ++i ) {
    //            switch( index ) {
    //                case 0: /*code from section 0*/
    //                case 1: /*code from section 1*/
    //                ...
    //            }
    //        }
    //        err = nanos_worksharing_next_item( data->wsd, ( void ** ) &nanos_item_loop );
    //    }
    SgStatement * while_cond =  buildExprStatement( buildDotExp( buildVarRefExp( nanos_item_loop ), 
                                                                 buildOpaqueVarRefExp( "execute", func_body ) ) );
    SgBasicBlock * while_body = buildBasicBlock( );
    SgVariableDeclaration * for_loop_it = buildVariableDeclaration( "nanos_item_iter", buildIntType( ), NULL, while_body );
    SgStatement * for_init = buildAssignStatement( buildVarRefExp( for_loop_it ), 
                                                   buildDotExp( buildVarRefExp( nanos_item_loop ), buildOpaqueVarRefExp( "lower", while_body ) ) );
    SgStatement * for_test = buildExprStatement( buildLessOrEqualOp( buildVarRefExp( for_loop_it ), 
                                                                     buildDotExp( buildVarRefExp( nanos_item_loop ), 
                                                                                  buildOpaqueVarRefExp( "upper", while_body ) ) ) );
    SgExpression * for_incr = buildPlusPlusOp( buildVarRefExp( for_loop_it ), SgUnaryOp::postfix );
    
    SgSwitchStatement * switch_stmt = buildSwitchStatement(  buildVarRefExp( for_loop_it ), buildBasicBlock( ) );
    Rose_STL_Container<SgNode *> section_list = NodeQuery::querySubTree( s, V_SgOmpSectionStatement );
    int section_count = section_list.size( );
    for( int i = 0; i < section_count; i++ )
    {
        SgBasicBlock * case_block = buildBasicBlock( isSgOmpSectionStatement( section_list[i] )->get_body( ) );
        
        // The lexically last section must contain the copy back statements ( lastprivate variables )
        if( i == section_count - 1 )
        {
            for( std::set<SgInitializedName *>::iterator it = restoreVars.begin( ); it != restoreVars.end( ); ++it )
            {
                SgInitializedName * orig_var = *it;
                ROSE_ASSERT( *it != NULL );
                std::string private_name = "_p_" + orig_var->get_name( );   // Lastprivates are always passed to the outlined function as pointer
        
                // We savely dereference this pointer because Nanos passes all Lastprivate variables by pointer
                SgStatement * copy_back_stmt = buildAssignStatement( buildVarRefExp( orig_var, case_block ),
                                                                     buildVarRefExp( private_name, case_block ) );
                appendStatement( copy_back_stmt, case_block );
            }
        }
        appendStatement( buildBreakStmt( ), case_block );
        SgCaseOptionStmt * option_stmt = buildCaseOptionStmt( buildIntVal( i ), case_block );
        
        switch_stmt->append_case( option_stmt );
    }
    
    SgForStatement * for_loop = buildForStatement( for_init, for_test, for_incr, switch_stmt );
    SgStatement * next_item_call = buildFunctionCallStmt( "nanos_worksharing_next_item", nanos_err_type, nanos_next_item_params, func_body );
    while_body->append_statement( for_loop_it );
    while_body->append_statement( for_loop );
    while_body->append_statement( next_item_call );
    SgWhileStmt * nanos_while = buildWhileStmt( while_cond, while_body );
    appendStatement( nanos_while, func_body );
    
    if( Outliner::useNewFile )
        ASTtools::setSourcePositionAtRootAndAllChildrenAsTransformation( func_body );
    
    //step 3: variable handling: create parameters, packing and unpacking statements and replace variables
    //---------------------------------------------------------------------------------------
    unpacking_stmts = Outliner::variableHandling( syms, pdSyms, restoreVars, struct_decl, func );
    
    //step 4: parameters checking
    //---------------------------------------------------------------------------------------
    // Member 'wsd' is only needed when there is a reduction performed sections construct
    // But we add it always to match the header of the Nanos sections method
    SgFunctionParameterList * params = func->get_parameterList( );
    SgInitializedName * wsd_param = buildInitializedName( "wsd", buildPointerType( buildOpaqueType( "nanos_ws_desc_t", func_body ) ) );
    SageInterface::appendArg( params, wsd_param );
    func->set_type( buildFunctionType( func->get_type()->get_return_type( ), 
                    buildFunctionParameterTypeList( params ) ) );
    
    // Retest this...
    ROSE_ASSERT( func_body->get_parent( ) == func->get_definition( ) );
    ROSE_ASSERT( scope->lookup_function_symbol( func->get_name( ) ) );
    
    return func;
}

//! A helper function to generate explicit section
//! Inspired in method 'generateOutlinedTask'
SgFunctionDeclaration* generateOutlinedSections( SgNode * node, std::string & wrapper_name, ASTtools::VarSymSet_t & syms, 
                                                 std::set<SgInitializedName *> & readOnlyVars, ASTtools::VarSymSet_t & pdSyms3, 
                                                 SgClassDeclaration * & struct_decl )
{
    ROSE_ASSERT( node != NULL );
    SgOmpClauseBodyStatement * target = isSgOmpClauseBodyStatement( node );
    ROSE_ASSERT( target != NULL );
    
    SgOmpSectionsStatement* omp_sections = isSgOmpSectionsStatement( node );
    ROSE_ASSERT( omp_sections != NULL );
    SgStatement * sections_body =  omp_sections->get_body( );
    ROSE_ASSERT( sections_body != NULL );
    
    // Initialize outliner 
    Outliner::useParameterWrapper = true; 
    Outliner::useStructureWrapper = true;
    SgBasicBlock* body_block = Outliner::preprocess( sections_body );
        
    // Variable handling is done after Outliner::preprocess() to ensure a basic block for the body,
    // but before calling the actual outlining.
    // This simplifies the outlining since firstprivate, private variables are replaced 
    // with their local copies before outliner is used 
    transOmpVariables( target, body_block );
    
    std::string func_name = Outliner::generateFuncName( target );
    SgGlobal* g_scope = SageInterface::getGlobalScope( node );
    ROSE_ASSERT( g_scope != NULL );
    
    // This step is less useful for private, firstprivate, and reduction variables
    // since they are already handled by transOmpVariables(). 
    Outliner::collectVars( body_block, syms );
    
    std::set<SgInitializedName *> restoreVars;
#ifdef USE_ROSE_NANOS_OPENMP_LIBRARY
    // We have to add the reduction symbols to the 'syms' list
    // because they have been considered firstprivate during "transOmpVariables"
    // and Nanos avoids the copyBack statement ( it is performed transparently ) 
    // that allows 'collectVars' to take into account these symbols
    ASTtools::VarSymSet_t red_syms;
    SgInitializedNamePtrList red_vars = collectClauseVariables( target, V_SgOmpReductionClause );
    convertAndFilter( red_vars, red_syms );
    set_union( syms.begin( ), syms.end(),
               red_syms.begin( ), red_syms.end( ),
               std::inserter( syms, syms.begin( ) ) );
    // Lastprivate variables have to be passed by pointer to allow the copy back in the outlined function
    ASTtools::VarSymSet_t lp_syms;
    SgInitializedNamePtrList lp_vars = collectClauseVariables( target, V_SgOmpLastprivateClause );
    convertAndFilter( lp_vars, lp_syms );
    set_union( syms.begin( ), syms.end(),
               lp_syms.begin( ), lp_syms.end( ),
               std::inserter( syms, syms.begin( ) ) );
    
    // Lastprivate variables must be copied back after sections execution, so we use restoreVars to store them
    restoreVars.insert( lp_vars.begin( ), lp_vars.end( ) );
#endif
    
    // Assume all parameters need to be passed by reference/pointers first
    ASTtools::VarSymSet_t pSyms, fpSyms,reductionSyms, pdSyms;
    std::copy( syms.begin( ), syms.end( ), std::inserter( pdSyms, pdSyms.begin( ) ) );
    
    // Exclude firstprivate variables: they are read only in fact
    // TODO keep class typed variables!!! even if they are firstprivate or private!! 
    SgInitializedNamePtrList fp_vars = collectClauseVariables( target, V_SgOmpFirstprivateClause );
    ASTtools::VarSymSet_t fp_syms, pdSyms2;
    convertAndFilter( fp_vars, fp_syms );
    set_difference( pdSyms.begin( ), pdSyms.end( ),
                    fp_syms.begin( ), fp_syms.end( ),
                    std::inserter( pdSyms2, pdSyms2.begin( ) ) );
    
    // Similarly , exclude private variable, also read only
    SgInitializedNamePtrList p_vars = collectClauseVariables( target, V_SgOmpPrivateClause );
    ASTtools::VarSymSet_t p_syms; //, pdSyms3;
    convertAndFilter( p_vars, p_syms );
    //TODO keep class typed variables!!! even if they are firstprivate or private!! 
    set_difference( pdSyms2.begin( ), pdSyms2.end( ),
                    p_syms.begin( ), p_syms.end( ),
                    std::inserter( pdSyms3, pdSyms3.begin( ) ) );

    // Add sizes of array variables when those are also variables
    ASTtools::VarSymSet_t new_syms;
    for (ASTtools::VarSymSet_t::const_iterator i = syms.begin (); i != syms.end (); ++i)
    {
        SgType* i_type = (*i)->get_declaration()->get_type();
    
        while (isSgArrayType(i_type))
        {
            // Get most significant dimension
            SgExpression* index = ((SgArrayType*) i_type)->get_index();
        
            // Get the variables used to compute the dimension
            // FIXME We insert a new statement and delete it afterwards in order to use "collectVars" function
            //       Think about implementing an specific function for expressions
            ASTtools::VarSymSet_t a_syms, a_pSyms;
            SgExprStatement* index_stmt = buildExprStatement(index);
            appendStatement(index_stmt, body_block);
            Outliner::collectVars(index_stmt, a_syms);
            SageInterface::removeStatement(index_stmt);
            for(ASTtools::VarSymSet_t::iterator j = a_syms.begin(); j != a_syms.end(); ++j)
            {
                const SgVariableSymbol* s = *j;
                new_syms.insert(s);   // If the symbol is not in the symbol list, it is added
            }
            
            // Advance over the type
            i_type = ((SgArrayType*) i_type)->get_base_type();
        }
    }
    for (ASTtools::VarSymSet_t::const_iterator i = new_syms.begin (); i != new_syms.end (); ++i)
    {
        const SgVariableSymbol* s = *i;
        syms.insert(s);
    }
    
    // Data structure used to wrap parameters
    struct_decl = Outliner::generateParameterStructureDeclaration( body_block, func_name, syms, pdSyms3, g_scope );
    
    SgFunctionDeclaration * result;

    //Generate the outlined function
    /* Parameter list
        SgBasicBlock* s,  // block to be outlined
        const string& func_name_str, // function name
        const ASTtools::VarSymSet_t& syms, // parameter list for all variables to be passed around
        const ASTtools::VarSymSet_t& pdSyms, // variables must use pointer dereferencing (pass-by-reference)
        const ASTtools::VarSymSet_t& psyms, // private or dead variables (not live-in, not live-out)
        SgClassDeclaration* struct_decl,  // an optional wrapper structure for parameters
        Depending on the internal flag, unpacking/unwrapping statements are generated inside the outlined function to use wrapper parameters.
     */
    std::set<SgVariableDeclaration *> unpacking_stmts;
    result = generateSectionsFunction( body_block, func_name, syms, pdSyms3, restoreVars, struct_decl, g_scope, unpacking_stmts );
    ROSE_ASSERT( result != NULL );
    
    Outliner::insert( result, g_scope, body_block );
    
    if( result->get_definingDeclaration( ) != NULL )
        SageInterface::setStatic( result->get_definingDeclaration( ) );
    if( result->get_firstNondefiningDeclaration( ) != NULL )
        SageInterface::setStatic( result->get_firstNondefiningDeclaration( ) );
    
    // We have to check here whether there is a reduction clause
    // Nanos outlines reductions in a different way than other RTLs
    // Whereas gomp and omni do the transformation in transOmpVariables 
    if( hasClause( target, V_SgOmpReductionClause ) )
    {
        // Modify the code inside the sections function in order to perform the reduction
        NanosLowering::generate_nanos_reduction( result, target, struct_decl, func_name, syms, pdSyms3, unpacking_stmts );
    }
    
    // Generate packing statements
    // must pass 'target', not 'body_block' to get the right scope in which the declarations are inserted
    wrapper_name = Outliner::generatePackingStatements( target, syms, pdSyms3, struct_decl );
    
    return result;
}

static bool unpacked_symbol_is_reduction_symbol( std::string unpack_sym_name, const ASTtools::VarSymSet_t& reduction_syms )
{
    bool res = false;
    for( ASTtools::VarSymSet_t::const_iterator it = reduction_syms.begin( ); it != reduction_syms.end( ); ++it )
    {
        if( unpack_sym_name == (*it)->get_name( ).getString( ) )
        {
            res = true;
            break;
        }
    }
    return res;
}

//! Create a function named 'func_name_str' containing an OpenMP reduction to be executed with Nanos
// 's' is the body of a function that contains an already outlined OpenMP construct 
// (parallel, loop or sections, depending on where the reduction has been defined)
// This function creates a new outlined function with the following modifications to 's'
// - Unpacking statements: all remain the same but those variables corresponding to the reduction operation. 
//   For them, the variable names are modified from NAME to g_th_NAME
// - The rest of statements in 's' remain the same, we substitute the shared variables 
//   corresponding to reduction symbols buy local copies
// - We add an extra statement per reduction to store the value local value computed by the current thread
// This new outlined function will be called by the Nanos reduction method, 
// which will be created by the function 'generateReductionWrapperFunction'
// The transformations in the code to generate a nanos reduction from a reduction defined in a parealle construct are:
// 1. Input:
//    void foo( ) {
//        #pragma omp parallel reductio(op:sym)
//    }
// 2. First transformation: outline the parallel
//    void foo( ) {
//        XOMP_parallel_for_NANOS( outlined_parallel_func, ... );
//    }
//    void outlined_parallel_func( void* __out_argv ) {
//         /* code generated by 'generateFunction', 'generateLoopFunction' or 'generateSectionsFunction' */
//    }
// 3. Second transformation: reoutline the parallel and create a wraper to perform the reduction
//    void foo( ) {
//        XOMP_parallel_for_NANOS( outlined_parallel_func, ... );
//    }
//    void outlined_parallel_func( void* __out_argv ) {     // This has become the wrapper
//         /* code generated by 'generateReductionWrapperFunction' */
//    }
//    void reoutlined_parallel_func( void *__out_argv, nanos_ws_desc_t *wsd ) {
//        /* code we are modifying in this function */
//    }
// Example:
// 1. For an input code:
//     int sum = 0;
//     int i = 1;
//     #pragma omp parallel reduction(+:sum) firstprivate(i)
//         sum=sum + i;
// 2. Nanos parallel construct outlining will generate an outlined function with the following statements:
//     int* sum = (int *)(((struct OUT__2__1527___data *)__out_argv) -> sum_p);
//     int i = (int )(((struct OUT__3__9808___data *)__out_argv) -> i);
//     int _p_i = i;
//     *sum = *sum + _p_i;
// 3. And this function will create a new outlined function modifying the previous statements as follows:
//     int (*g_th_sum)[256] = (int (*)[256])(((struct OUT__2__1527___data *)__out_argv) -> g_th_sum_p);
//     int i = (int )(((struct OUT__3__9808___data *)__out_argv) -> i);
//     int _p_i = i;
//     int _p_sum = 0;
//     _p_sum = _p_sum + _p_i;
//     ( *g_th_sum)[XOMP_get_nanos_thread_num()] = _p_sum;
static SgFunctionDeclaration * generateReductionFunction( 
        SgBasicBlock* s, const std::string& func_name_str,
        const ASTtools::VarSymSet_t& reduction_syms,
        SgClassDeclaration* struct_decl, SgScopeStatement* scope, 
        std::set<SgVariableDeclaration *>& unpacking_stmts )
{
    ROSE_ASSERT( s && scope );
    ROSE_ASSERT( isSgGlobal( scope ) );
    
    //step 1. Create function skeleton, 'func'
    //---------------------------------------------------------------------------------------
    SgFunctionParameterList* parameterList = buildFunctionParameterList( );
    SgFunctionDeclaration* func = Outliner::createFuncSkeleton( func_name_str, buildVoidType( ), parameterList, scope );
    ROSE_ASSERT( func );
    if( SageInterface::is_Cxx_language( ) || is_mixed_C_and_Cxx_language( ) )
    {   // Enable C code to call this outlined function making the function 'extern "C"'
        func->get_declarationModifier( ).get_storageModifier( ).setExtern( );
        func->set_linkage( "C" );
    }
    
    //step 2. Create the function body
    //---------------------------------------------------------------------------------------
    SgBasicBlock* func_body = func->get_definition( )->get_body( );
    ROSE_ASSERT( func_body != NULL );
    // Copy the block to the new function avoiding the unpacking statements
    SgStatementPtrList reduction_stmts = s->get_statements( );
    ROSE_ASSERT( !reduction_stmts.empty( ) );
    SgType * struct_type = struct_decl->get_type( );
    SgClassDefinition * struct_def = struct_decl->get_definition( );
    for( SgStatementPtrList::iterator it = reduction_stmts.begin( ); it != reduction_stmts.end( ); ++it )
    {
        SgVariableDeclaration * it_decl = isSgVariableDeclaration( *it );
        SgStatement * copied_stmt;
        if( ( it_decl == NULL ) || ( unpacking_stmts.find( it_decl ) == unpacking_stmts.end( ) ) )
        {   // This is an not an unpacking statement
            copied_stmt = SageInterface::copyStatement( *it );
        }
        else
        {   // For the variables that are reduction symbols, we transform the variable name form NAME to g_th_NAME
            // otherwise, we just insert the unpacking statement as it was
            SgInitializedNamePtrList vars = it_decl->get_variables( );
            ROSE_ASSERT( vars.size( ) == 1 );
            SgInitializedName * old_unpack_init_name = * vars.begin( );
            const SgVariableSymbol * old_unpack_sym = isSgVariableSymbol( old_unpack_init_name->search_for_symbol_from_symbol_table( ) );
            ROSE_ASSERT( old_unpack_sym != NULL );
            if( unpacked_symbol_is_reduction_symbol( old_unpack_sym->get_name( ), reduction_syms ) )
            {
                std::string new_unpack_name = "g_th_" + old_unpack_init_name->get_name( );
                SgPointerType * unpack_sym_type = isSgPointerType( old_unpack_sym->get_type( ) );
                ROSE_ASSERT( unpack_sym_type != NULL );
                SgType * new_unpack_type = buildPointerType( buildArrayType( unpack_sym_type->get_base_type( ), buildIntVal( 256 ) ) );
                // We use "__out_argv" as it is used in "variableHandling" method.
                SgExpression * new_unpack_expr = buildArrowExp( buildCastExp( buildVarRefExp( "__out_argv", scope ),
                                                                              buildPointerType( struct_type ) ),
                                                                buildVarRefExp( new_unpack_name + "_p", struct_def ) );
                SgInitializer * new_unpack_init = buildAssignInitializer( buildCastExp( new_unpack_expr, new_unpack_type ) );
                copied_stmt = buildVariableDeclaration( new_unpack_name, new_unpack_type, new_unpack_init, scope );
            }
            else
            {
                copied_stmt = SageInterface::copyStatement( it_decl );
            }
        }
        appendStatement( copied_stmt, func_body );
    }
    // Add the current thread computation to the array containing all threads computation
    for( ASTtools::VarSymSet_t::const_iterator i = reduction_syms.begin (); i != reduction_syms.end (); ++i )
    {
        SgFunctionCallExp* func_call_exp = buildFunctionCallExp( "XOMP_get_nanos_thread_num", buildIntType( ), 
                                                                 buildExprListExp( ), func_body );
        const SgVariableSymbol* r_sym = isSgVariableSymbol( *i );
        SgExprStatement* thread_reduction = 
                buildAssignStatement( buildPntrArrRefExp( buildPointerDerefExp( buildVarRefExp( "g_th_" + r_sym->get_name( ), func_body ) ), 
                                                          func_call_exp ),
                                      buildVarRefExp( "_p_" + r_sym->get_name( ), func_body ) );
        appendStatement( thread_reduction, func_body );
    }
    if( Outliner::useNewFile )
        ASTtools::setSourcePositionAtRootAndAllChildrenAsTransformation( func_body );
    
    //step 3: variable handling: create parameters, packing and unpacking statements and replace variables
    //---------------------------------------------------------------------------------------
    // We don't need to do this again, since the statements in the original 's' already passed by this when we outlined the parallel / sections / loop
    // std::set<SgInitializedName *> restoreVars;
    // variableHandling( syms, pdSyms, restoreVars, struct_decl, func );

    //step 4: parameters checking
    //---------------------------------------------------------------------------------------
    // Parameters should be created during 'variableHandling', but since we don't call this function, we create them manually
    // The parameters we need here are:
    //    - __out_argv: data struct with the variables to the outlined function
    //    - wsd: is only needed when the reduction is made on a sections construct, but we add it always to match the Nanos header
    SgFunctionParameterList * params = func->get_parameterList( );
    SgInitializedName * out_argv_param = buildInitializedName( "__out_argv", buildPointerType( buildVoidType( ) ) );
    SgInitializedName * wsd_param = buildInitializedName( "wsd", buildPointerType( buildOpaqueType( "nanos_ws_desc_t", func_body ) ) );
    SageInterface::appendArg( params, out_argv_param );
    SageInterface::appendArg( params, wsd_param );
    func->set_type( buildFunctionType( func->get_type()->get_return_type( ),
                    buildFunctionParameterTypeList( params ) ) );
    
    // Retest this...
    ROSE_ASSERT( func_body->get_parent( ) == func->get_definition( ) );
    ROSE_ASSERT( scope->lookup_function_symbol( func->get_name( ) ) );
    
    return func;
}

// This method generates the expression that performs a reduction over 'lhs' and 'rhs' depending on the reduction operation
static SgStatement * build_nanos_reduction_loop_body_from_operation( SgExpression * lhs, SgExpression * rhs, 
                                                                     SgOmpClause::omp_reduction_operator_enum r_operator )
{
    SgStatement * loop_body;
    switch (r_operator) 
    {
        case SgOmpClause::e_omp_reduction_plus:
            loop_body = buildExprStatement( buildPlusAssignOp( lhs, rhs) );
            break;
        case SgOmpClause::e_omp_reduction_mul:
            loop_body = buildExprStatement( buildMultAssignOp( lhs, rhs) );
            break;
        case SgOmpClause::e_omp_reduction_minus:
            loop_body = buildExprStatement( buildMinusAssignOp( lhs, rhs) );
            break;
        case SgOmpClause::e_omp_reduction_ior:
            loop_body = buildExprStatement( buildIorAssignOp( lhs, rhs) );
            break;
        case SgOmpClause::e_omp_reduction_bitand:
        case SgOmpClause::e_omp_reduction_bitor:
        case SgOmpClause::e_omp_reduction_bitxor: 
        case SgOmpClause::e_omp_reduction_iand:
        case SgOmpClause::e_omp_reduction_logand:
        case SgOmpClause::e_omp_reduction_logor:
        case SgOmpClause::e_omp_reduction_eqv: 
        case SgOmpClause::e_omp_reduction_neqv:
        case SgOmpClause::e_omp_reduction_max:
        case SgOmpClause::e_omp_reduction_min:
        case SgOmpClause::e_omp_reduction_ieor:
        default:
            std::stringstream op; op << r_operator;
            std::string message = "Illegal or unhandled reduction operator type " + op.str( ) + " for Nanos reductions";
            ROSE_ABORT( message.c_str( ) );
    }
    return loop_body;
}

// This method generates a wraper over an already outlined function to perform a reduction
// This is the second part of the process of generating a Nanos reduction, 
// as is specified in the documentation of the function 'generateReductionFunction'
// Following the example for 'generateReductionFunction', this method creates a function containing the following code:
//     static void OUT__1__7705__(void *__out_argv)
//         // Unpacking statements: original packed statements that are now in the reduction wrapping function
//         int *sum = (int *)(((struct OUT__3__9808___data *)__out_argv) -> sum_p);
//         int i = (int )(((struct OUT__3__9808___data *)__out_argv) -> i);
//         // Array to store the local values computed by each thread
//         int g_th_sum[XOMP_get_nanos_num_threads()];
//         // Packing statements to the outlided function that will perform the reduction code
//         // this includes an array of #threads elements to store each thread reduction value
//         struct OUT__3__1527___data __out_argv3__1527__;
//         __out_argv3__1527__.g_th_sum_p = &g_th_sum;
//         __out_argv3__1527__.i = i;
//         void *g_array[1] = {(sum)};                      // Global symbol (for each reduction variable)
//         long g_size_array[1] = {sizeof(int )};           // Size of the reduction variable (for each reduction variable)
//         void *g_th_array[1] = {((void **)(&g_th_sum))};  // Global array storing local values (for each reduction variable)
//         // Functions performing the reduction of each thread values into a unique reduction value (for each reduction variable)
//         void (*__nanos_all_thread_reduction_array__[1])(void *, void *, int ) = {((void (*)(void *, void *, int ))__nanos_all_threads_reduction__)};
//         // Function copying the nanos internal values into the original variables (for each reduction variable)
//         void (*__nanos_copy_back_reduction_array__[1])(int , void *, void *) = {((void (*)(int , void *, void *))__nanos_copy_back_reduction__)};
//         // Function copying the value computed for each thread into a nanos internal variable (for each reduction variable)
//         // FIXME This is not correct because we cannot control that this is perform when needed!
//         void (*__nanos_set_privates_array__[1])(void *, void **, int , int ) = {((void (*)(void *, void **, int , int ))__nanos_set_privates__)};
//         nanos_ws_desc_t *wsd;                                                        // Work descriptor storing Nanos information
//         // Actual call to the Nanos reduction method
//          XOMP_reduction_for_NANOS( 1, __nanos_all_thread_reduction_array__, reduction_func, &__out_argv3__1527__, 
//                                    __nanos_copy_back_reduction_array__, __nanos_set_privates_array__, 
//                                    g_th_array, g_array, g_size_array, wsd, file_name, #line);
//     }
// This method also created the functions introduced above for data movement:
//     static void __nanos_all_threads_reduction__( int *omp_out, int *omp_in, int num_scalars ) {
//         for (int i = 0; i < num_scalars; ++i) 
//             omp_out[i] += omp_in[i];
//     }
//     static void __nanos_copy_back_reduction__( int team_size, void *original, void *privates ) {
//         for (int i = 0; i < team_size; ++i)
//             *((int *)original) += ((int *)privates)[i];
//     }
//     static void __nanos_set_privates__( void *nanos_privates, void **global_data, int reduction_id, int thread ) {
//         ((int *)nanos_privates)[thread] = ((int **)global_data)[reduction_id][thread];
//     }
static void generateReductionWrapperFunction(
        SgOmpClauseBodyStatement * target, 
        SgFunctionDeclaration * func, const std::string& func_name_str,
        const ASTtools::VarSymSet_t& syms, const ASTtools::VarSymSet_t& pdSyms,
        const ASTtools::VarSymSet_t& reduction_syms,
        SgClassDeclaration* struct_decl, SgScopeStatement* scope, 
        std::set<SgVariableDeclaration *>& unpacking_stmts )
{
    // 1. Create the function body containing
    // ---------------------------------------------------------------------------
    SgBasicBlock * old_func_body = func->get_definition( )->get_body( );
    SgBasicBlock * new_func_body = buildBasicBlock( );
    replaceStatement( old_func_body, new_func_body );
    
    // The unpacking statements remain as they were originally
    for( std::set<SgVariableDeclaration *>::iterator it = unpacking_stmts.begin( ); it != unpacking_stmts.end( ); ++it )
    {
        new_func_body->append_statement( *it );
    }
    
    int n_reductions = reduction_syms.size( );
    SgExprListExp * global_data_initializers = buildExprListExp( );
    SgExprListExp * global_data_size_initializers = buildExprListExp( );
    SgExprListExp * global_th_data_initializers = buildExprListExp( );
    SgType * g_th_type = buildPointerType( buildVoidType( ) );
    for( ASTtools::VarSymSet_t::const_iterator rs = reduction_syms.begin (); rs != reduction_syms.end (); ++rs )
    {
        // Reduction symbol declaration: int g_th_sum[XOMP_get_nanos_num_threads()];
        const SgVariableSymbol* r_sym = isSgVariableSymbol( *rs );
        SgName current_global_data_name = "g_th_" + r_sym->get_name( );
        SgFunctionCallExp* get_num_threads = buildFunctionCallExp( "XOMP_get_nanos_num_threads", buildIntType( ), 
                buildExprListExp( ), new_func_body );
        SgVariableDeclaration* global_data_array_decl = 
                buildVariableDeclaration( current_global_data_name, buildArrayType( r_sym->get_type( ), get_num_threads ),
                                                  /*init*/ NULL, new_func_body );
        new_func_body->append_statement( global_data_array_decl );
        
        // Initializer of the current symbol for the array of symbols: {((void **)(&g_th_sum))}
        global_data_initializers->append_expression( 
                buildAssignInitializer( buildVarRefExp( r_sym->get_name( ), new_func_body ), 
                                        buildPointerType( buildVoidType( ) ) ) );
        global_data_size_initializers->append_expression( buildSizeOfOp( r_sym->get_type( ) ) );
        global_th_data_initializers->append_expression( 
                buildAssignInitializer( buildCastExp( buildAddressOfOp( buildVarRefExp( current_global_data_name, new_func_body ) ),
                                                      buildPointerType( g_th_type ) ),
                                        buildPointerType( g_th_type ) ) );
    }
    // Array of original reduction symbols
    SgAggregateInitializer * global_data_array_initializer = buildAggregateInitializer( global_data_initializers, g_th_type );
    SgName global_data_array_name = "g_array";
    SgVariableDeclaration * global_data_array = 
            buildVariableDeclaration( global_data_array_name, 
                                      buildArrayType( g_th_type, buildIntVal( n_reductions ) ),
                                      global_data_array_initializer, new_func_body );
    new_func_body->append_statement( global_data_array );
    // Array of sizes
    SgAggregateInitializer * global_data_size_array_initializer = buildAggregateInitializer( global_data_size_initializers, g_th_type );
    SgName global_data_size_array_name = "g_size_array";
    SgVariableDeclaration * global_data_size_array = 
            buildVariableDeclaration( global_data_size_array_name, 
                                      buildArrayType( buildLongType( ), buildIntVal( n_reductions ) ),
                                      global_data_size_array_initializer, new_func_body );
    new_func_body->append_statement( global_data_size_array );
    // Array of arrays of per-thread reduction values
    SgAggregateInitializer * global_th_data_array_initializer = 
            buildAggregateInitializer( global_th_data_initializers, buildPointerType( g_th_type ) );
    SgName global_th_data_array_name = "g_th_array";
    SgVariableDeclaration * global_th_data_array = 
            buildVariableDeclaration( global_th_data_array_name, 
                                      buildArrayType( g_th_type, buildIntVal( n_reductions ) ),
                                      global_th_data_array_initializer, new_func_body );
    new_func_body->append_statement( global_th_data_array );
    
    // We must call this function here, so we already have the statement 
    // that is going to be immediately after the packing statements ( g_array )
    ASTtools::VarSymSet_t reduction_pdSyms;
    reduction_pdSyms.insert( pdSyms.begin( ), pdSyms.end( ) );
    reduction_pdSyms.insert( reduction_syms.begin( ), reduction_syms.end( ) );
    std::string reduction_wrapper_name = Outliner::generatePackingStatements( global_data_array, syms, pdSyms,
                                                                              struct_decl, reduction_syms );
    
    // 2. Create the function that computes the reduction of each thread partial reduction
    //    There will be one function per reduction
    // ---------------------------------------------------------------------------
    //    static void __nanos_all_threads_reduction__( int *omp_out, int *omp_in, int num_scalars ) {
    //        for (int i = 0; i < num_scalars; ++i) 
    //            omp_out[i] += omp_in[i];
    //    }
    SgExprListExp * nanos_all_th_red_funcs_array = buildExprListExp( );
    SgType * all_th_reduc_func_return_type = buildVoidType( );
    SgFunctionType* all_th_reduction_func_type = 
            buildFunctionType( all_th_reduc_func_return_type, 
                               buildFunctionParameterTypeList( buildPointerType( buildVoidType( ) ), 
                                                               buildPointerType( buildVoidType( ) ),
                                                               buildIntType( ) ) );
    for( ASTtools::VarSymSet_t::const_iterator rs = reduction_syms.begin (); rs != reduction_syms.end (); ++rs )
    {
        SgName out_data = "omp_out";
        SgName in_data = "omp_in";
        SgName n_scalars_data = "num_scalars";
        SgName iterator = "i";
        SgName all_th_func_name = SageInterface::generateUniqueVariableName( scope, "nanos_all_threads_reduction" );
        
        // Build the function definition
        SgType * r_sym_type = isSgVariableSymbol( *rs )->get_type( );
        SgFunctionParameterList * params = buildFunctionParameterList( );
        SgInitializedName * out_data_param = buildInitializedName( out_data, buildPointerType( r_sym_type ) );
        appendArg( params, out_data_param );
        SgInitializedName * in_data_param = buildInitializedName( in_data, buildPointerType( r_sym_type ) );
        appendArg( params, in_data_param );
        SgInitializedName * n_scalar_param = buildInitializedName( n_scalars_data, buildIntType( ) );
        appendArg( params, n_scalar_param );
        SgFunctionDeclaration* func_def = buildDefiningFunctionDeclaration( all_th_func_name, all_th_reduc_func_return_type, params, scope );
        SageInterface::setStatic( func_def );
        insertStatementAfter( func, func_def );

        SgBasicBlock* func_body = func_def->get_definition( )->get_body( );
        ROSE_ASSERT( func_body != NULL );
        
        SgVariableDeclaration * loop_iter = buildVariableDeclaration( iterator, buildIntType( ), NULL, func_body );
        appendStatement( loop_iter, func_body ); 
        SgStatement * loop_init = buildAssignStatement( buildVarRefExp( iterator, func_body ), buildIntVal( 0 ) );
        SgStatement * loop_test = buildExprStatement( buildLessThanOp( buildVarRefExp( iterator, func_body ), 
                buildVarRefExp( n_scalars_data, func_body ) ) );
        SgExpression * loop_incr = buildPlusPlusOp( buildVarRefExp( iterator, func_body ), SgUnaryOp::prefix );
        
        SgExpression * lhs = buildPntrArrRefExp( buildVarRefExp( out_data, func_body ), 
                buildVarRefExp( iterator, func_body ) );
        SgExpression * rhs = buildPntrArrRefExp( buildVarRefExp( in_data, func_body ), 
                buildVarRefExp( iterator, func_body ) );
        SgOmpClause::omp_reduction_operator_enum r_operator 
                = OmpSupport::getReductionOperationType( isSgVariableSymbol( *rs )->get_declaration( ), target );
        SgStatement * loop_body = build_nanos_reduction_loop_body_from_operation( lhs, rhs, r_operator );
        
        SgForStatement * all_th_red = buildForStatement( loop_init, loop_test, loop_incr, loop_body );
        appendStatement( all_th_red, func_body );

        // Build the function declaration
        SgFunctionDeclaration* func_decl = buildNondefiningFunctionDeclaration( func_def, scope );
        SageInterface::setStatic( func_decl );
        insertStatementBefore( func, func_decl );
        
        // Add the function expression to the initializer array to be passed to nanos
        SgFunctionSymbol * func_sym = isSgFunctionSymbol( func_decl->get_symbol_from_symbol_table( ) );
        SgExpression * all_th_reduction_func_casted_ref = buildAssignInitializer( buildCastExp( buildFunctionRefExp( func_sym ), 
                buildPointerType( all_th_reduction_func_type ) ),
        buildPointerType( all_th_reduction_func_type ) );
        nanos_all_th_red_funcs_array->append_expression( all_th_reduction_func_casted_ref );
    }
    SgAggregateInitializer * nanos_all_th_red_initializer = 
            buildAggregateInitializer( nanos_all_th_red_funcs_array, buildPointerType( all_th_reduction_func_type ) );
    SgName nanos_all_th_red_funcs_array_name = SageInterface::generateUniqueVariableName( new_func_body, "nanos_all_thread_reduction_array_" );
    SgVariableDeclaration * nanos_all_th_red_func_array = 
            buildVariableDeclaration( nanos_all_th_red_funcs_array_name, 
                                      buildArrayType( buildPointerType( all_th_reduction_func_type ), buildIntVal( reduction_syms.size( ) ) ),
                                      nanos_all_th_red_initializer, new_func_body );
    new_func_body->append_statement( nanos_all_th_red_func_array );
    
    // 3. Create the copy back function for the reduction values
    //    There will be one function per reduction
    // ---------------------------------------------------------------------------
    //     static void __nanos_copy_back_reduction__( int team_size, void *original, void *privates ) {
    //         for (int i = 0; i < team_size; ++i)
    //             *((int *)original) += ((int *)privates)[i];
    //     }
    SgExprListExp * nanos_copy_back_red_funcs_array = buildExprListExp( );
    SgType * copy_back_reduc_func_return_type = buildVoidType( );
    SgFunctionType* copy_back_reduction_func_type = 
            buildFunctionType( copy_back_reduc_func_return_type, 
                               buildFunctionParameterTypeList( buildIntType( ),
                                       buildPointerType( buildVoidType( ) ),
                                       buildPointerType( buildVoidType( ) ) ) );
    
    for( ASTtools::VarSymSet_t::const_iterator rs = reduction_syms.begin (); rs != reduction_syms.end (); ++rs )
    {
        SgName team_size = "team_size";
        SgName original = "original";
        SgName privates = "privates";
        SgName iterator = "i";
        SgName copy_back_func_name = SageInterface::generateUniqueVariableName( scope, "nanos_copy_back_reduction" );
        
        // Build the function definition
        SgType * r_sym_type = isSgVariableSymbol( *rs )->get_type( );
        SgFunctionParameterList * params = buildFunctionParameterList( );
        SgInitializedName * team_size_param = buildInitializedName( team_size, buildIntType( ) );
        appendArg( params, team_size_param );      
        SgInitializedName * original_param = buildInitializedName( original, buildPointerType( buildVoidType( ) ) );
        appendArg( params, original_param );
        SgInitializedName * privates_param = buildInitializedName( privates, buildPointerType( buildVoidType( ) ) );
        appendArg( params, privates_param );
        SgFunctionDeclaration* func_def = buildDefiningFunctionDeclaration( copy_back_func_name, copy_back_reduc_func_return_type, params, scope );
        SageInterface::setStatic( func_def );
        insertStatementAfter( func, func_def );

        SgBasicBlock* func_body = func_def->get_definition( )->get_body( );
        ROSE_ASSERT( func_body != NULL );
        
        SgVariableDeclaration * loop_iter = buildVariableDeclaration( iterator, buildIntType( ), NULL, func_body );
        appendStatement( loop_iter, func_body ); 
        SgStatement * loop_init = buildAssignStatement( buildVarRefExp( iterator, func_body ), buildIntVal( 0 ) );
        SgStatement * loop_test = buildExprStatement( buildLessThanOp( buildVarRefExp( iterator, func_body ), 
                buildVarRefExp( team_size, func_body ) ) );
        SgExpression * loop_incr = buildPlusPlusOp( buildVarRefExp( iterator, func_body ), SgUnaryOp::prefix );
        
        SgExpression * lhs = buildPointerDerefExp( buildCastExp( buildVarRefExp( original, func_body ), buildPointerType( r_sym_type ) ) );
        SgExpression * rhs = buildPntrArrRefExp( buildCastExp( buildVarRefExp( privates, func_body ), buildPointerType( r_sym_type ) ), 
                buildVarRefExp( iterator, func_body ) );
        SgOmpClause::omp_reduction_operator_enum r_operator 
                = OmpSupport::getReductionOperationType( isSgVariableSymbol( *rs )->get_declaration( ), target );
        SgStatement * loop_body = build_nanos_reduction_loop_body_from_operation( lhs, rhs, r_operator );
        
        SgForStatement * copy_back_red = buildForStatement( loop_init, loop_test, loop_incr, loop_body );
        appendStatement( copy_back_red, func_body );
        
        // Build the function declaration
        SgFunctionDeclaration* func_decl = buildNondefiningFunctionDeclaration( func_def, scope );
        SageInterface::setStatic( func_decl );
        insertStatementBefore( func, func_decl );
        
        // Add the function expression to the initializer array to be passed to nanos
        SgFunctionSymbol * func_sym = isSgFunctionSymbol( func_decl->get_symbol_from_symbol_table( ) );
        SgExpression * copy_back_reduction_func_casted_ref = buildAssignInitializer( buildCastExp( buildFunctionRefExp( func_sym ), 
                buildPointerType( copy_back_reduction_func_type ) ),
        buildPointerType( copy_back_reduction_func_type ) );
        nanos_copy_back_red_funcs_array->append_expression( copy_back_reduction_func_casted_ref );
    }
    SgAggregateInitializer * nanos_copy_back_red_initializer = 
            buildAggregateInitializer( nanos_copy_back_red_funcs_array, buildPointerType( copy_back_reduction_func_type ) );
    SgName nanos_copy_back_red_funcs_array_name = SageInterface::generateUniqueVariableName( new_func_body, "nanos_copy_back_reduction_array_" );
    SgVariableDeclaration * nanos_copy_back_red_func_array = 
            buildVariableDeclaration( nanos_copy_back_red_funcs_array_name, 
                                      buildArrayType( buildPointerType( copy_back_reduction_func_type ), buildIntVal( reduction_syms.size( ) ) ),
                                      nanos_copy_back_red_initializer, new_func_body );
    new_func_body->append_statement( nanos_copy_back_red_func_array );
    
    // 4. Generate the function that copies the global values computed in the outlined reduction funtion
    //    into the nanos private copies member 
    //    There will be one function per reduction
    // ---------------------------------------------------------------------------
    //    static void __nanos_set_privates__( void *nanos_privates, void **global_data, int reduction_id, int thread ) {
    //        ((int *)nanos_privates)[thread] = ((int **)global_data)[reduction_id][thread];
    //    }
    SgExprListExp * set_privates_func_inits = buildExprListExp( );
    SgType * set_privates_func_return_type = buildVoidType( );
    SgFunctionType* set_privates_func_type = 
            buildFunctionType( set_privates_func_return_type, 
                               buildFunctionParameterTypeList( buildPointerType( buildVoidType( ) ),
                                       buildPointerType( buildPointerType( buildVoidType( ) ) ), buildIntType( ), buildIntType( ) ) );
    
    for( ASTtools::VarSymSet_t::const_iterator rs = reduction_syms.begin (); rs != reduction_syms.end (); ++rs )
    {
        SgName nanos_privates = "nanos_privates";
        SgName global_data = "global_data";
        SgName reduction_id = "reduction_id";
        SgName thread = "thread";
        
        SgName set_privates_func_name = SageInterface::generateUniqueVariableName( scope, "nanos_set_privates" );
        
        // Build the function definition
        SgType * r_sym_type = isSgVariableSymbol( *rs )->get_type( );
        SgFunctionParameterList * params = buildFunctionParameterList( );
        SgInitializedName * nanos_privates_param = buildInitializedName( nanos_privates, buildPointerType( buildVoidType( ) ) );
        appendArg( params, nanos_privates_param );
        SgInitializedName * global_data_param = buildInitializedName( global_data, buildPointerType( buildPointerType( buildVoidType( ) ) ) );
        appendArg( params, global_data_param );
        SgInitializedName * reduction_id_param = buildInitializedName( reduction_id, buildIntType( ) );
        appendArg( params, reduction_id_param );
        SgInitializedName * thread_param = buildInitializedName( thread, buildIntType( ) );
        appendArg( params, thread_param );
        SgFunctionDeclaration* func_def = buildDefiningFunctionDeclaration( set_privates_func_name, set_privates_func_return_type, params, scope );
        SageInterface::setStatic( func_def );
        insertStatementAfter( func, func_def );

        SgBasicBlock* func_body = func_def->get_definition( )->get_body( );
        ROSE_ASSERT( func_body != NULL );
        
        SgExpression * lhs = buildPntrArrRefExp( buildCastExp( buildVarRefExp( nanos_privates, func_body ), buildPointerType( r_sym_type ) ), 
                buildVarRefExp( thread, func_body ) );
        SgExpression * rhs = buildPntrArrRefExp( buildPntrArrRefExp( buildCastExp( buildVarRefExp( global_data, func_body ), 
                buildPointerType( buildPointerType( r_sym_type ) ) ),
        buildVarRefExp( reduction_id, func_body ) ),
        buildVarRefExp( thread, func_body ) );
        SgStatement * set_private_stmt = buildAssignStatement( lhs, rhs );
        appendStatement( set_private_stmt, func_body );
        
        // Build the function declaration
        SgFunctionDeclaration* func_decl = buildNondefiningFunctionDeclaration( func_def, scope );
        SageInterface::setStatic( func_decl );
        insertStatementBefore( func, func_decl );
        
        // Add the function expression to the initializer array to be passed to nanos
        SgFunctionSymbol * func_sym = isSgFunctionSymbol( func_decl->get_symbol_from_symbol_table( ) );
        SgExpression * set_privates_func_casted_ref = buildAssignInitializer( buildCastExp( buildFunctionRefExp( func_sym ), 
                buildPointerType( set_privates_func_type ) ),
        buildPointerType( set_privates_func_type ) );
        set_privates_func_inits->append_expression( set_privates_func_casted_ref );
    }
    SgAggregateInitializer * set_privates_initializer = 
            buildAggregateInitializer( set_privates_func_inits, buildPointerType( set_privates_func_type ) );
    SgName set_privates_array_name = SageInterface::generateUniqueVariableName( new_func_body, "nanos_set_privates_array_" );
    SgVariableDeclaration * set_privates_func_array = 
            buildVariableDeclaration( set_privates_array_name, 
                                      buildArrayType( buildPointerType( set_privates_func_type ), buildIntVal( reduction_syms.size( ) ) ),
                                      set_privates_initializer, new_func_body );
    new_func_body->append_statement( set_privates_func_array );
    
    // 5. Generate the WSD (WorkSahring Descriptor) variable.
    // ---------------------------------------------------------------------------
    // When Reductions are applied to sections, this value is initialized in libnanos.c::NANOS_sections and passed as a parameter.
    // Otherwise, we don't need it, so we create an empty variable to fulfill the header requirements
    // Example:
    //      before: static void OUT__1__6706__( void *__out_argv ) { ... }
    //      after:  static void OUT__1__6706__( void *__out_argv, nanos_ws_desc_t *wsd ) { ... }
    std::string wsd_name = "wsd";
    SgInitializedName * wsd_init_name;
    SgInitializedNamePtrList params = func->get_parameterList( )->get_args( );
    if( params.size( ) == 2 )
    {
        wsd_init_name = params[1];
        if( wsd_init_name->get_name( ) != wsd_name )
        {
            std::cerr << "Expected 'wsd' as second parameter of an outlined reduction with Nanos RTL. " 
                      << "Argument found is '" << wsd_init_name->get_name( ) << "'" << std::endl;
            ROSE_ABORT( );
        }
    }
    else
    {
        SgVariableDeclaration * wsd_declaration = 
                buildVariableDeclaration( wsd_name, buildPointerType( buildOpaqueType( "nanos_ws_desc_t", new_func_body ) ), 
                                          NULL, new_func_body );
        new_func_body->append_statement( wsd_declaration );
        wsd_init_name = wsd_declaration->get_decl_item( wsd_name );
    }
    
    // 7. Generate the call to the Nanos reduction function
    // ---------------------------------------------------------------------------
    SgExprListExp* parameters = NULL;
    SgExpression * p1_num_reductions = buildIntVal( reduction_syms.size( ) );
    SgExpression * p2_all_th_reductions = buildVarRefExp( nanos_all_th_red_funcs_array_name, new_func_body );
    SgExpression * p3_single_th_reduction = buildFunctionRefExp( isSgFunctionSymbol( scope->lookup_function_symbol( func_name_str ) ) );
    SgExpression * p4_single_th_data = buildAddressOfOp( buildVarRefExp( reduction_wrapper_name, new_func_body ) );
    SgExpression * p5_copy_back_reduction = buildVarRefExp( nanos_copy_back_red_funcs_array_name, new_func_body );
    SgExpression * p6_set_privates = buildVarRefExp( set_privates_array_name, new_func_body );
    SgExpression * p7_global_th_datas = buildVarRefExp( global_th_data_array_name, new_func_body );
    SgExpression * p8_global_datas = buildVarRefExp( global_data_array_name, new_func_body );
    SgExpression * p9_global_data_sizes = buildVarRefExp( global_data_size_array_name, new_func_body );
    SgExpression * p10_wsd = buildVarRefExp( wsd_init_name, new_func_body );
    SgExpression * p11_filename = buildStringVal( target->get_file_info( )->get_filename( ) );
    SgExpression * p12_fileline = buildIntVal( target->get_file_info( )->get_line( ) );
    std::vector<SgExpression *> param_list;
    param_list.push_back( p1_num_reductions );
    param_list.push_back( p2_all_th_reductions );
    param_list.push_back( p3_single_th_reduction );
    param_list.push_back( p4_single_th_data );
    param_list.push_back( p5_copy_back_reduction );
    param_list.push_back( p6_set_privates );
    param_list.push_back( p7_global_th_datas );
    param_list.push_back( p8_global_datas );
    param_list.push_back( p9_global_data_sizes );
    param_list.push_back( p10_wsd );
    param_list.push_back( p11_filename );
    param_list.push_back( p12_fileline );
    parameters = buildExprListExp( param_list );
    SgExprStatement * nanos_reduction_call = buildFunctionCallStmt( "XOMP_reduction_for_NANOS", buildVoidType( ), 
            parameters, new_func_body );
    new_func_body->append_statement( nanos_reduction_call );
}

// This method outlines an OpenMP reduction:
//     1. 'code' is moved from 'func' to 're-outlined_func'
//     2. extra statements are added to 'code' to execute the nanos reduction
//     3. 'func' body is replaced by a the new wrapping code 
// Example:
//     input:    func { code }
//     output:   func {         // wrapper func
//                  unpack statements
//                  pack statements
//                  nanos reduction variables 
//                  XOMP_reduction_for_NANOS( re-outlined_func, ... )
//               }
//               re-outlined_func { code with some transformations for nanos reduction }
void generate_nanos_reduction( SgFunctionDeclaration * func,
                               SgOmpClauseBodyStatement * target, 
                               SgClassDeclaration*& struct_decl, 
                               std::string func_name,
                               ASTtools::VarSymSet_t syms, ASTtools::VarSymSet_t pdSyms, 
                               std::set<SgVariableDeclaration *> unpacking_stmts )
{
    SgGlobal* g_scope = SageInterface::getGlobalScope( func );
    
    // 1. Re-outline the code containing the reduction code into a new function
    //    Create the additional statments that performs the per-thread reduction
    // ---------------------------------------------------------------------------
    std::string reduction_func_name = Outliner::generateFuncName( func );
    
    VariantVector vvt = VariantVector( V_SgOmpReductionClause );
    SgInitializedNamePtrList reduction_vars = collectClauseVariables( target, vvt );
    ASTtools::VarSymSet_t reduction_syms;
    convertAndFilter( reduction_vars, reduction_syms );
    syms.insert( reduction_syms.begin( ), reduction_syms.end( ) );
    pdSyms.insert( reduction_syms.begin( ), reduction_syms.end( ) );
    SgBasicBlock * reduction_code = func->get_definition( )->get_body( );
    // Create the new outlined function
    SgClassDeclaration * reduction_struct_decl = 
            Outliner::generateParameterStructureDeclaration( reduction_code, reduction_func_name, syms, pdSyms, 
                                                             g_scope, /*nanos_ws*/ '0', reduction_syms );
    SgBasicBlock * outlined_reduction_body = func->get_definition( )->get_body( );
    SgFunctionDeclaration * reduction = 
            generateReductionFunction( outlined_reduction_body, reduction_func_name, 
                                       reduction_syms, reduction_struct_decl, g_scope, unpacking_stmts );
    ROSE_ASSERT( reduction != NULL );
    Outliner::insert( reduction, g_scope, reduction_code );
    if( reduction->get_definingDeclaration( ) != NULL )
        SageInterface::setStatic( reduction->get_definingDeclaration( ) );
    if( reduction->get_firstNondefiningDeclaration( ) != NULL )
        SageInterface::setStatic( reduction->get_firstNondefiningDeclaration( ) );
    
    // 2. Outline the function that will call the Nanos reduction method
    generateReductionWrapperFunction( target, func, reduction_func_name, syms, pdSyms, reduction_syms, 
                                      reduction_struct_decl, g_scope, unpacking_stmts );
}

// This method inserts a new empty struct of a given type before a given statement
// This empty struct will be used to call Nanos specifix XOMP functions
// For constructs other than parallel, we generate only one empty struct
// Example:
//     struct OUT__1__7331___data *empty___out_argv1__7331__ = (struct OUT__1__7331___data *)0;
SgExpression* build_nanos_empty_struct( SgStatement* omp_stmt, SgScopeStatement* stmt_sc, 
                                        SgType* struct_type, std::string base_name )
{
    SgName empty_struct_name = "empty_" + base_name;
    SgAssignInitializer * init = buildAssignInitializer( buildCastExp( buildIntVal( 0 ), buildPointerType( struct_type ) ) );
    SgVariableDeclaration* empty_struct_decl = buildVariableDeclaration( empty_struct_name, buildPointerType( struct_type ), 
                                                                         init, stmt_sc );
    SageInterface::insertStatementBefore(omp_stmt, empty_struct_decl);
  
    return buildVarRefExp( empty_struct_name, stmt_sc );
}

// This method generates an empty struct of a given tye allocated dinamically and returns it
// Nanos requires a struct filled with the proper parameters and a struct of the same type but empty to execute properly
// When executing a parallel construct, the number of empty structs we need depends on the number of threads executing,
// and we only can know this value dinamically
// Example:
//     static struct OUT__1__7812___data *get_empty_struct___out_argv2__7812__() {
//         struct OUT__1__7812___data *empty___out_argv2__7812__ = 0;
//         return empty___out_argv2__7812__;
//     }
SgExpression* build_nanos_get_empty_struct( SgStatement* ancestor, SgScopeStatement* expr_sc, 
                                            SgType* struct_type, std::string base_name )
{
    // void* (*get_empty_data)()
    SgScopeStatement* decl_sc = ancestor->get_scope( );
    ROSE_ASSERT( decl_sc != NULL );
      
    // Build the definition and insert it after the englobing funtion of the original OpenMP pragma
    SgName func_name = "get_empty_struct_" + base_name;
    SgType * pt_struct_type = buildPointerType( struct_type );
    SgFunctionDeclaration * func_def = buildDefiningFunctionDeclaration( func_name, pt_struct_type, 
                                                                         buildFunctionParameterList( ), decl_sc );
    SageInterface::setStatic( func_def );
    insertStatementAfter( ancestor, func_def );
    
    // Create the body of the function
    SgBasicBlock* func_body = func_def->get_definition( )->get_body( );
    ROSE_ASSERT( func_body != NULL );
    
    // Add the expression to the function body
    SgName empty_struct_name = "empty_" + base_name;
    SgInitializer* struct_init = buildAssignInitializer( buildIntVal( 0 ), pt_struct_type );
    SgVariableDeclaration* empty_struct_decl = buildVariableDeclaration( empty_struct_name, pt_struct_type, 
                                                                         struct_init, expr_sc );
    appendStatement( empty_struct_decl, func_body );
    
    SgStatement * return_stmt = buildReturnStmt( buildVarRefExp( empty_struct_name, expr_sc ) );
    appendStatement( return_stmt, func_body );
    
      // Build the definition and insert it before the englobing funtion of the original OpenMP pragma
    SgFunctionDeclaration * func_decl = buildNondefiningFunctionDeclaration( func_def, decl_sc );
    SageInterface::setStatic( func_decl );
    insertStatementBefore( ancestor, func_decl );
    
    SgFunctionSymbol * func_sym = isSgFunctionSymbol( func_decl->get_symbol_from_symbol_table( ) );
    SgFunctionType * casting_func_type = buildFunctionType( buildPointerType( buildVoidType( ) ), 
                                                            buildFunctionParameterTypeList( ) );
    return buildCastExp( buildFunctionRefExp( func_sym ), buildPointerType( casting_func_type ) );
}

// This method initialized the members of an empty struct with the values in an initialized struct
// Example:
//     static void init_struct___out_argv1__7331__(struct OUT__1__7331___data *empty_data, struct OUT__1__7331___data *initialized_data) {
//         empty_data->member_1 = initialized_data->member_1;
//         ...
//         empty_data->member_N = initialized_data->member_N;
//    }
SgExpression* build_nanos_init_arguments_struct_function( SgStatement* ancestor, std::string& wrapper_name, 
                                                          SgClassDeclaration* struct_decl )
{
    SgScopeStatement* decl_sc = ancestor->get_scope( );
    ROSE_ASSERT( decl_sc != NULL );
    
    // Build the definition and insert it after the englobing funtion of the original OpenMP pragma
    SgFunctionParameterList* params = buildFunctionParameterList( );
    
    // parameters
    ROSE_ASSERT( struct_decl != NULL );
    SgPointerType * pt_struct_type = buildPointerType( struct_decl->get_type( ) );
    SgInitializedName* empty_st_param = buildInitializedName( "empty_data", pt_struct_type );
    appendArg( params, empty_st_param );
    SgInitializedName * init_st_param = buildInitializedName( "initialized_data", pt_struct_type );
    appendArg( params, init_st_param );

    // Create the declaration of the function
    SgName func_name = "init_struct_" + wrapper_name;
    SgFunctionDeclaration* func_def = SageBuilder::buildDefiningFunctionDeclaration( func_name, buildVoidType( ), 
                                                                                     params, decl_sc );
    SageInterface::setStatic( func_def );
    insertStatementAfter( ancestor, func_def );
  
    // Create the body of the function
    SgClassDefinition* struct_def = struct_decl->get_definition( );
    ROSE_ASSERT( struct_def != NULL );
    SgDeclarationStatementPtrList st_members = struct_def->get_members( );
    if( !st_members.empty( ) )
    {
        
        SgBasicBlock* func_body = func_def->get_definition( )->get_body( );
        ROSE_ASSERT( func_body != NULL );
        
        SgInitializedNamePtrList param_names = params->get_args( );
        Rose_STL_Container<SgInitializedName*>::iterator params_i = param_names.begin( );
        ROSE_ASSERT(param_names.size() == 2);
        
        SgVarRefExp* empty_st_expr = buildVarRefExp( isSgInitializedName( *params_i ), func_body );
        SgVarRefExp* init_st_expr = buildVarRefExp( isSgInitializedName( *++params_i ), func_body );
        
        Rose_STL_Container<SgDeclarationStatement*>::iterator member_i;
        for( member_i = st_members.begin( ); member_i != st_members.end( ); ++member_i )
        {
            // Get the current member
            SgVariableDeclaration* member = isSgVariableDeclaration( *( member_i ) );
            if( member != NULL )
            {
                SgInitializedNamePtrList& member_name_list = member->get_variables( );
                SgInitializedName* member_name = *( member_name_list.begin( ) );
                
                // Create the initialization expression
                SgVarRefExp* member_expr = buildVarRefExp( member_name, func_body );
                SgExpression* lhs = buildArrowExp( empty_st_expr, member_expr );
                SgExpression* rhs = buildArrowExp( init_st_expr, member_expr );
                
                // Add the expression to the function body
                SgBasicBlock * init_member = generateArrayAssignmentStatements( lhs, rhs );
                appendStatement( init_member, func_body );
            }
        }
    }
  
  // Create the definition and insert it before the englobing funtion of the original OpenMP pragma
  SgFunctionDeclaration* func_decl = buildNondefiningFunctionDeclaration( func_def, decl_sc );
  SageInterface::setStatic( func_decl );
  insertStatementBefore( ancestor, func_decl );
  
  SgFunctionSymbol* func_sym = isSgFunctionSymbol( func_decl->get_symbol_from_symbol_table( ) );
  SgFunctionType* casting_func_type = 
          buildFunctionType( buildVoidType( ), 
                             buildFunctionParameterTypeList( buildPointerType( buildVoidType( ) ), 
                                                             buildPointerType( buildVoidType( ) ) ) );
  return buildCastExp( buildFunctionRefExp( func_sym ), buildPointerType( casting_func_type ) );
}

// This method generates a function that returns the alignment of a struct declaration
// The alignment is needed to execute any openmp directive with nanos, and it can only obtained dinamically
// Example:
//     static long get_alignof___out_argv3__7705__() {
//         return __alignof__(struct OUT__3__7705___data);
//     }
SgExpression* build_nanos_get_alignof( SgStatement* ancestor, std::string& wrapper_name, SgClassDeclaration* struct_decl )
{
    SgScopeStatement* decl_sc = ancestor->get_scope( );
    ROSE_ASSERT( decl_sc != NULL );
    
    // Build the definition and insert it after the englobing funtion of the original OpenMP pragma
    SgFunctionParameterList* params = buildFunctionParameterList( );
    SgName func_name = "get_alignof_" + wrapper_name;
    SgFunctionDeclaration* func_def = buildDefiningFunctionDeclaration( func_name, buildLongType( ), 
                                                                        params, decl_sc );
    SageInterface::setStatic( func_def );
    insertStatementAfter( ancestor, func_def );
    
    // Create the body of the function
    SgBasicBlock* func_body = func_def->get_definition( )->get_body( );
    ROSE_ASSERT( func_body != NULL );
    SgSymbol* struct_sym = struct_decl->search_for_symbol_from_symbol_table( );
    // FIXME: We should use here:
    // AstUnparseAttribute* code = new AstUnparseAttribute("__alignof__(struct " + struct_sym->get_name() + ");", AstUnparseAttribute::e_inside);
    // ROSE_ASSERT(code != NULL);
    // func_body->addNewAttribute("alignof", code);
    attachArbitraryText( func_body, "  return __alignof__(struct " + struct_sym->get_name() + ");", PreprocessingInfo::inside );
    
    // Build the definition and insert it before the englobing funtion of the original OpenMP pragma
    SgFunctionDeclaration* func_decl = buildNondefiningFunctionDeclaration(func_def, decl_sc );
    SageInterface::setStatic( func_decl );
    insertStatementBefore( ancestor, func_decl );
    
    SgFunctionSymbol* func_sym = isSgFunctionSymbol( func_decl->get_symbol_from_symbol_table( ) );
    return buildFunctionRefExp( func_sym );
}

// Parse dependency clauses from 'task' and store expressions and dependency direction
void get_dependency_clauses( SgOmpTaskStatement * task, SgExprListExp * & dependences_direction, 
                             SgExprListExp * & dependences_data, int & n_deps )
{
    ROSE_ASSERT( task != NULL );
    n_deps = 0;  
    Rose_STL_Container<SgOmpClause*> depend_clauses = getClause( task, V_SgOmpDependClause );
    if( !depend_clauses.empty( ) )
    {
        for( Rose_STL_Container<SgOmpClause*>::const_iterator iter = depend_clauses.begin( ); 
             iter != depend_clauses.end( ); iter++ )
        {   // dimension map is the same for all the map clauses under the same omp task directive
            SgOmpDependClause* d_cls = isSgOmpDependClause( *iter );
            ROSE_ASSERT (d_cls != NULL);
            SgOmpClause::omp_depend_operator_enum depend_operator = d_cls->get_operation( );
            SgExpressionPtrList deps = isSgOmpExpressionsClause( isSgOmpDependClause( *iter ) )->get_expressions( );
            
            SgExpression * dep_direction;
            if( depend_operator == SgOmpClause::e_omp_depend_in )
            {
                dep_direction = buildIntVal( e_dep_dir_in );
            }
            else if( depend_operator == SgOmpClause::e_omp_depend_out )
            {
                dep_direction = buildIntVal( e_dep_dir_out );
            }
            else if( depend_operator == SgOmpClause::e_omp_depend_inout )
            {
                dep_direction = buildIntVal( e_dep_dir_inout );
            }
            else 
            {
                std::cerr << "Error. get_dependency_clauses() from omp_lowering.cpp: " 
                          << "found unacceptable map operator type:" << depend_operator << std::endl;
                ROSE_ASSERT( false );
            }
            
            for( SgExpressionPtrList::iterator it = deps.begin( ); it != deps.end( ); it++ )
            {
                dependences_direction->append_expression( dep_direction );
                dependences_data->append_expression( *it );
                n_deps++;
            }
        }
    }
}

// This method computes the base symbol of an expression of a dependency clause
// The possible cases are:
//   - scalar S               ->  S
//   - array A[x]             ->  A
//   - shape expression [x]A  ->  A
//   - array section A[x:y]   ->  A
// FIXME We may have pointers here!
SgVarRefExp * get_dependecy_base_symbol_exp( SgExpression * dep_expr )
{
    SgExpression * result_tmp = dep_expr;
    if( isSgShapeExpression( result_tmp ) )
        result_tmp = isSgShapeExpression( result_tmp )->get_rhs_operand( );
    
    while( isSgPntrArrRefExp( result_tmp ) )
    {
        result_tmp = isSgPntrArrRefExp( result_tmp )->get_lhs_operand( );
    }
    SgVarRefExp * result = isSgVarRefExp( result_tmp );
    ROSE_ASSERT( result != NULL );
    
    return result;
}

// Create the dependency arrays containing
//        - the dependencies direction (input, output, inout)
//        - the dependencies data (object which is the actual dependency)
// The parameters of the function define which of the two arrays is created
// This method return a reference to the created array 
// Example:
//        Input code:
//            #pragma omp task inout(x) input(y)
//            {}
//        Code generated with this method:
//            int __deps_direction0__[] = {(0), (2)}; // base_array_name="deps_direction", array_type=int []
//            void *__deps_data1__[] = {(y), (x)};    // base_array_name="deps_data", array_type=void* []
SgExpression * build_nanos_dependencies_array( SgExprListExp * dependences, std::string & array_name, SgArrayType * array_type,
                                               SgOmpTaskStatement * task, SgScopeStatement * scope, bool build_data )
{
    array_name = SageInterface::generateUniqueVariableName( scope, array_name );
    SgExprListExp * initializers = buildExprListExp( );
    SgExpressionPtrList dependences_exprs = dependences->get_expressions( );
    if( build_data )
    {
        for( SgExpressionPtrList::iterator dep = dependences_exprs.begin( ); dep != dependences_exprs.end( ); dep++ )
        {
            SgExpression * base_expr = get_dependecy_base_symbol_exp( *dep );
            initializers->append_expression( buildAssignInitializer( buildAddressOfOp( base_expr ), buildPointerType( buildVoidType( ) ) ) );
        }
    }
    else
    {
        for( SgExpressionPtrList::iterator dep=dependences_exprs.begin( ); dep != dependences_exprs.end( ); dep++ )
        {
            initializers->append_expression( buildAssignInitializer( *dep, buildPointerType( buildVoidType( ) ) ) );
        } 
    }

    SgInitializer* initializers_array = buildAggregateInitializer( initializers, array_type );
    SgVariableDeclaration* array_decl = buildVariableDeclaration( SgName( array_name ), array_type, initializers_array, scope );
    SageInterface::insertStatementBefore( task, array_decl );
    
    return buildVarRefExp( array_name, scope );
}


// This method builds, for each dependency, an array of 'nanos_region_dimension_t' with n_dim elements
// Example:
//     {
//         { size_t /*dimension size*/, size_t /*lower bound accessed*/, size_t /*length accessed*/}, /*Dim 0*/
//         ...,
//         { size_t /*dimension size*/, size_t /*lower bound accessed*/, size_t /*length accessed*/}  /*Dim N*/
//     }
void build_nanos_dependencies_dimension_array( std::string & all_dims_name, std::string & n_dims_name, std::string & offsets_name,
                                               SgExprListExp * dependences_data, 
                                               SgOmpTaskStatement * task, SgScopeStatement * scope,
                                               SgExpression * & all_dims_ref, SgExpression * & n_dims_ref, SgExpression * & offsets_ref )
{
    SgExprListExp * all_deps_initializers = buildExprListExp( );
    
    // Build an array containing the number of dimensions of each dependency
    SgExprListExp * n_dims_initializers = buildExprListExp( );
    
    // Build an array containing the offset of each dependency
    SgExprListExp * offsets_initializers = buildExprListExp( );
    
    // Create the 'nanos_region_dimension_t' in the Global scope, so it is only created once
    SgType * region_dim_type = buildOpaqueType( "nanos_region_dimension_t", getGlobalScope( task ) );
    
    unsigned int n_deps = 0;
    SgExpressionPtrList dependency_exprs = dependences_data->get_expressions( );
    for(SgExpressionPtrList::iterator dep = dependency_exprs.begin( ); dep != dependency_exprs.end( ); dep++ )
    {
        // Get the current dependence dimensions characteristics: size, lower bound, upper bound, length
        SgExpression* dep_exp = *dep;
        SgExprListExp * dep_dims_initializers = buildExprListExp( );
        
        // Get the list of array access, if exist
        SgExprListExp * array_accesses_l = buildExprListExp( );
        SgExpression * dep_exp_tmp = dep_exp;
        while( isSgPntrArrRefExp( dep_exp_tmp ) )
        {
            array_accesses_l->prepend_expression( isSgPntrArrRefExp( dep_exp_tmp )->get_rhs_operand( ) );
            dep_exp_tmp = isSgPntrArrRefExp( dep_exp_tmp )->get_lhs_operand( );
        }
        
        // Get the size of dimensions, if exist
        SgExprListExp * array_dims_l = buildExprListExp( );
        unsigned int n_dims = 0;
        if( isSgShapeExpression( dep_exp ) )
        {
            array_dims_l = isSgExprListExp( isSgShapeExpression( dep_exp )->get_lhs_operand( ) );
            n_dims = array_dims_l->get_expressions( ).size( );
        }
        // After reshaping, we can have array type with the
        {
            SgVariableSymbol * base_sym = get_dependecy_base_symbol_exp( *dep )->get_symbol( );
            SgType * dep_type_tmp = base_sym->get_type( )->stripType( SgType::STRIP_POINTER_TYPE | SgType::STRIP_TYPEDEF_TYPE );
            while( isSgArrayType( dep_type_tmp ) )
            {
                n_dims++;
                array_dims_l->append_expression( isSgArrayType( dep_type_tmp )->get_index( ) );
                dep_type_tmp = isSgArrayType( dep_type_tmp )->get_base_type( );
            }
            if( isSgPointerType( dep_type_tmp ) )
            {
                std::cerr << "Dependencies in a variable of pointer type without specifying a size are not supported" << std::endl;
                ROSE_ABORT( );
            }
        }
        
        SgExpressionPtrList array_accesses = array_accesses_l->get_expressions( );
        SgExpressionPtrList array_dims = array_dims_l->get_expressions( );
        if( array_dims.size( ) < array_accesses.size( ) )
        {
            for( unsigned int i = array_dims.size( ); i < array_accesses.size(); ++i )
            {
                SgExpression * access = array_accesses[array_accesses.size() - i];
                SgExpression * dim_size;
                if( isSgArraySectionExp( access ) )
                {
                    dim_size = isSgArraySectionExp( access )->get_length( );
                }
                else
                {
                    dim_size = buildIntVal( 1 );
                }
                array_dims_l->prepend_expression( dim_size );
                n_dims++;
            }
            array_dims = array_dims_l->get_expressions( );
        }
        else if( array_dims.size( ) > array_accesses.size( ) )
        {
            for( unsigned int i = array_accesses.size( ); i < array_dims.size(); ++i )
            {
                array_accesses_l->prepend_expression( array_dims[i] );
            }
            array_accesses = array_accesses_l->get_expressions( );
        }
        ROSE_ASSERT( array_dims.size( ) == array_accesses.size( ) );
        
        SgType * dep_base_type = dep_exp->get_type( )->stripType( SgType::STRIP_POINTER_TYPE | SgType::STRIP_ARRAY_TYPE | SgType::STRIP_TYPEDEF_TYPE );
        SgExpression * dim_size, * lower_bound, * length;
        if( n_dims == 0 )
        {
            dim_size =  buildIntVal( 1 );
            lower_bound = buildIntVal( 0 );
            length = buildIntVal( 1 );
            
            dep_dims_initializers->prepend_expression( buildAggregateInitializer( 
                    buildExprListExp( buildAssignInitializer( dim_size ), 
                                      buildAssignInitializer( lower_bound ), 
                                      buildAssignInitializer( length ) ) ) );
        }
        else
        {
            for( unsigned int i = 0; i < n_dims; ++i )
            {
                if( i < array_accesses.size( ) )
                {   // Only the accessed region becomes dependence
                    SgExpression * current_access = array_accesses[i];
                    if( isSgArraySectionExp( current_access ) )
                    {   // A section is defined for the current dimension
                        lower_bound = isSgArraySectionExp( current_access )->get_lower_bound( );
                        length = isSgArraySectionExp( current_access )->get_length( );
                    }
                    else
                    {   // Single element access
                        lower_bound = current_access;
                        length = buildIntVal( 1 );
                    }
                }
                else
                {   // No access, the whole dimension is a dependence
                    lower_bound = buildIntVal( 0 );
                    length = buildSubtractOp( array_dims[i], buildIntVal( 1 ) );
                }
                
                if( i < n_dims - 1 )
                {   // Dimensions other thant the least significant one are expressed in units
                    dep_dims_initializers->prepend_expression( buildAggregateInitializer (
                            buildExprListExp( buildAssignInitializer( array_dims[i] ), 
                                            buildAssignInitializer( lower_bound ), 
                                            buildAssignInitializer( length ) ) ) );
                }
                else
                {   // Least sifgnificant dimension (the contiguous one) must be expressed in bytes
                    dep_dims_initializers->prepend_expression( buildAggregateInitializer (
                            buildExprListExp( buildAssignInitializer( buildMultiplyOp( buildSizeOfOp( dep_base_type ), array_dims[i] ) ), 
                                              buildAssignInitializer( buildMultiplyOp( buildSizeOfOp( dep_base_type ), lower_bound ) ),
                                              buildAssignInitializer( buildMultiplyOp( buildSizeOfOp( dep_base_type ), length ) ) ) ) );
                }
            }
        }

        // Create the actual array with the dependence dimensions information
        SgType * dep_dims_type = buildArrayType( region_dim_type, buildIntVal( n_dims ) );
        SgInitializer * dep_dims_initializers_array = buildAggregateInitializer( dep_dims_initializers, dep_dims_type );
        SgVariableDeclaration * dep_dims_array = buildVariableDeclaration( SageInterface::generateUniqueVariableName( scope, "dep_dims" ), 
                                                                           dep_dims_type, dep_dims_initializers_array, scope );
        SageInterface::insertStatementBefore( task, dep_dims_array );
        all_deps_initializers->append_expression( buildVarRefExp( dep_dims_array ) );
        
        // Create the current dependence initializer for the array of number of dimensions
        n_dims_initializers->append_expression( buildAssignInitializer( buildIntVal( n_dims ) ) );
        
        // TODO Calculate the offset of the current dependency
        offsets_initializers->append_expression( 
                buildAssignInitializer( buildMultiplyOp( buildSizeOfOp( dep_base_type ), lower_bound ) ) );
        
        n_deps++;
    }
    // Create the array of dimensions
    all_dims_name = SageInterface::generateUniqueVariableName( scope, all_dims_name );
    SgType * all_dims_type = buildArrayType( buildPointerType( region_dim_type ), buildIntVal( n_deps ) );
    SgInitializer * all_dims_initializers_array = buildAggregateInitializer( all_deps_initializers, all_dims_type );
    SgVariableDeclaration * all_dims_array_decl = buildVariableDeclaration( all_dims_name, all_dims_type,
                                                                            all_dims_initializers_array, scope );
    SageInterface::insertStatementBefore( task, all_dims_array_decl );
    all_dims_ref = buildVarRefExp( all_dims_name, scope );
      
    // Create the array with the number of dimensions
    n_dims_name = SageInterface::generateUniqueVariableName( scope, n_dims_name );
    SgType * n_dims_type = buildArrayType( buildIntType( ), buildIntVal( n_deps ) );
    SgInitializer * n_dims_initializers_array = buildAggregateInitializer( n_dims_initializers, n_dims_type );
    SgVariableDeclaration * n_dims_array_decl = buildVariableDeclaration( n_dims_name, n_dims_type,
                                                                          n_dims_initializers_array, scope );
    SageInterface::insertStatementBefore( task, n_dims_array_decl );
    n_dims_ref = buildVarRefExp( n_dims_name, scope );
    
    // Create the array with the offset of each dependency
    offsets_name = SageInterface::generateUniqueVariableName( scope, offsets_name );
    SgType * offsets_type = buildArrayType( buildLongType( ), buildIntVal( n_deps ) );
    SgInitializer * offsets_initializers_array = buildAggregateInitializer( offsets_initializers, offsets_type );
    SgVariableDeclaration * offsets_array_decl = buildVariableDeclaration( offsets_name, offsets_type, 
                                                                           offsets_initializers_array, scope );
    SageInterface::insertStatementBefore( task, offsets_array_decl );
    offsets_ref = buildVarRefExp( offsets_name, scope );
}

}
}