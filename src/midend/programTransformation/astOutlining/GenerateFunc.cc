/**
 *  \file Transform/GenerateFunc.cc
 *
 *  \brief Generates an outlined (independent) C-callable function
 *  from an SgBasicBlock.
 *
 *  This outlining implementation specifically generates C-callable
 *  routines for use in an empirical tuning application. Such routines
 *  can be isolated into their own, dynamically shareable modules.
 */
// tps (01/14/2010) : Switching from rose.h to sage3.
#include "rose_config.h"
#include "sage3basic.h"
#include "sageBuilder.h"
#include <iostream>
#include <string>
#include <sstream>
#include <set>


#include "Outliner.hh"
#include "ASTtools.hh"
#include "VarSym.hh"
#include "Copy.hh"
#include "StmtRewrite.hh"
#include "Outliner.hh"

//! Stores a variable symbol remapping.
typedef std::map<const SgVariableSymbol *, SgVariableSymbol *> VarSymRemap_t;

// =====================================================================

using namespace std;
using namespace SageInterface;
using namespace SageBuilder;

/* ===========================================================
 */

//! Creates a non-member function.
static
SgFunctionDeclaration *
createFuncSkeleton (const string& name, SgType* ret_type,
                    SgFunctionParameterList* params, SgScopeStatement* scope)
   {
     ROSE_ASSERT(scope != NULL);
     ROSE_ASSERT(isSgGlobal(scope)!=NULL);
     SgFunctionDeclaration* func;
     SgProcedureHeaderStatement* fortranRoutine;
  // Liao 12/13/2007, generate SgProcedureHeaderStatement for Fortran code
     if (SageInterface::is_Fortran_language()) 
        {
          fortranRoutine = SageBuilder::buildProcedureHeaderStatement(name.c_str(),ret_type, params, SgProcedureHeaderStatement::e_subroutine_subprogram_kind,scope);
          func = isSgFunctionDeclaration(fortranRoutine);  
        }
       else
        {
          func = SageBuilder::buildDefiningFunctionDeclaration(name,ret_type,params,scope);
        }

     ROSE_ASSERT (func != NULL);

   SgFunctionSymbol* func_symbol = scope->lookup_function_symbol(func->get_name());
   ROSE_ASSERT(func_symbol != NULL);
   if (Outliner::enable_debug)
   {
     printf("Found function symbol in %p for function:%s\n",scope,func->get_name().getString().c_str());
   }
     return func;
   }

// ===========================================================

//! Creates an SgInitializedName.
static
SgInitializedName *
createInitName (const string& name, SgType* type,
                SgDeclarationStatement* decl,
                SgScopeStatement* scope,
                SgInitializer* init = 0)
{
  SgName sg_name (name.c_str ());

// DQ (2/24/2009): Added assertion.
  ROSE_ASSERT(name.empty() == false);
  SgInitializedName* new_name = new SgInitializedName (ASTtools::newFileInfo (), sg_name, type, init,decl, scope, 0);
  setOneSourcePositionForTransformation (new_name);
  ROSE_ASSERT (new_name);
  // Insert symbol
  if (scope)
    {
      SgVariableSymbol* new_sym = new SgVariableSymbol (new_name);
      scope->insert_symbol (sg_name, new_sym);
      ROSE_ASSERT (new_sym->get_parent() != NULL);
    }
  ROSE_ASSERT (new_name->get_endOfConstruct() != NULL);

  return new_name;
}


//! Returns 'true' if the base type is a primitive type.
static
bool
isBaseTypePrimitive (const SgType* type)
{
  if (!type) return false;
  const SgType* base_type = type->findBaseType ();
  if (base_type)
    switch (base_type->variantT ())
      {
      case V_SgTypeBool:
      case V_SgTypeChar:
      case V_SgTypeDouble:
      case V_SgTypeFloat:
      case V_SgTypeInt:
      case V_SgTypeLong:
      case V_SgTypeLongDouble:
      case V_SgTypeLongLong:
      case V_SgTypeShort:
      case V_SgTypeSignedChar:
      case V_SgTypeSignedInt:
      case V_SgTypeSignedLong:
      case V_SgTypeSignedShort:
      case V_SgTypeUnsignedChar:
      case V_SgTypeUnsignedInt:
      case V_SgTypeUnsignedLong:
      case V_SgTypeUnsignedShort:
      case V_SgTypeVoid:
      case V_SgTypeWchar:
        return true;
      default:
        break;
      }
  return false;
}

//! Stores a new outlined-function parameter.
typedef std::pair<string, SgType *> OutlinedFuncParam_t;

/*!
 *  \brief Creates a new outlined-function parameter for a given
 *  variable. The requirement is to preserve data read/write semantics.
 *  
 *  This function is only used when wrapper parameter is not used
 *  so individual parameter needs to be created for each variable passed to the outlined function.
 *
 *  For C/C++: we use pointer dereferencing to implement pass-by-reference
 *    So the parameter needs to be &a, which is a pointer type of a's base type
 *
 *    In a recent implementation, side effect analysis is used to find out
 *    variables which are not modified so pointer types are not used.
 *
 *  For Fortran, all parameters are passed by reference by default.
 *
 *  Given a variable (i.e., its type and name) whose references are to
 *  be outlined, create a suitable outlined-function parameter. 
 *  For C/C++, the  parameter is created as a pointer, to support parameter passing of
 *  aggregate types in C programs. 
 *  Moreover, the type is made 'void' if the base type is not a primitive type.
 *   
 *  An original type may need adjustments before we can make a pointer type from it.
 *  For example: 
 *    a)Array types from a function parameter: its first dimension is auto converted to a pointer type 
 *
 *    b) Pointer to a C++ reference type is illegal, we create a pointer to its
 *    base type in this case. It also match the semantics for addressof(refType) 
 *
 * 
 *  The implementation follows two steps:
 *     step 1: adjust a variable's base type
 *     step 2: decide on its function parameter type
 *  Liao, 8/14/2009
 */
static
OutlinedFuncParam_t
createParam (const SgInitializedName* i_name,  // the variable to be passed into the outlined function
             bool classic_original_type=false) // flag to decide if the variable's adjusted type is used directly, only applicable when -rose:outline:enable_classic  is turned on
{
  ROSE_ASSERT (i_name);
  SgType* init_type = i_name->get_type();
  ROSE_ASSERT (init_type);

  // Store the adjusted original types into param_base_type
  // primitive types: --> original type
  // complex types: void
  // array types from function parameters:  pointer type for 1st dimension
  // C++ reference type: use base type since we want to have uniform way to generate a pointer to the original type
  SgType* param_base_type = 0;
  if (isBaseTypePrimitive (init_type)||Outliner::enable_classic)
    // for classic translation, there is no additional unpacking statement to 
    // convert void* type to non-primitive type of the parameter
    // So we don't convert the type to void* here
  {
    // Duplicate the initial type.
    param_base_type = init_type; //!< \todo Is shallow copy here OK?
    //param_base_type = const_cast<SgType *> (init_type); //!< \todo Is shallow copy here OK?
   
    // Adjust the original types for array or function types (TODO function types) which are passed as function parameters 
    // convert the first dimension of an array type function parameter to a pointer type, 
    // This is called the auto type conversion for function or array typed variables 
    // that are passed as function parameters
    // Liao 4/24/2009
    if (!SageInterface::is_Fortran_language() ) // Only apply to C/C++, not Fortran!
    {
      if (isSgArrayType(param_base_type)) 
        if (isSgFunctionDefinition(i_name->get_scope()))
          param_base_type= SageBuilder::buildPointerType(isSgArrayType(param_base_type)->get_base_type());
    }
     
    //For C++ reference type, we use its base type since pointer to a reference type is not allowed
    //Liao, 8/14/2009
    SgReferenceType* ref = isSgReferenceType (param_base_type);
    if (ref != NULL)
      param_base_type = ref->get_base_type();

    ROSE_ASSERT (param_base_type);
  }
  else // for non-primitive types, we use void as its base type
  {
    param_base_type = SgTypeVoid::createType ();
    ROSE_ASSERT (param_base_type);
    //Take advantage of the const modifier
    if (ASTtools::isConstObj (init_type))
    {
      SgModifierType* mod = SageBuilder::buildConstType(param_base_type);
      param_base_type = mod;
    }
  }

  // Stores the real parameter type to be used in new_param_type
   string init_name = i_name->get_name ().str (); 
   // The parameter name reflects the type: the same name means the same type, 
   // p__ means a pointer type
  string new_param_name = init_name;
  SgType* new_param_type = NULL;

  // For classic behavior, read only variables are passed by values for C/C++
  // They share the same name and type
  if (Outliner::enable_classic) 
  { 
    // read only parameter: pass-by-value, the same type and name
    if (classic_original_type )
    {
      new_param_type = param_base_type;
    }
    else
    {
      new_param_name+= "p__";
      new_param_type = SgPointerType::createType (param_base_type);
    }
  }
  else // The big assumption of this function is within the context of no wrapper parameter is used 
    // very conservative one, assume the worst side effects (all are written) 
      //TODO, why not use  classic_original_type to control this!!??
  { 
    ROSE_ASSERT (Outliner::useParameterWrapper == false && Outliner::useStructureWrapper == false);
    if (!SageInterface::is_Fortran_language())
    {
      new_param_type = SgPointerType::createType (param_base_type);
      ROSE_ASSERT (new_param_type);
      new_param_name+= "p__";
    }
    else
    {
      // Fortran:
      // Liao 1/19/2010
      // We have to keep the parameter names the same as the original ones
      // Otherwise, we have to deep copy the SgArrayType used in the outlined portion and replace the name within dim info expression list.
      //
      // In an outlined function: 
      // e.g. INTEGER :: s_N
      //      DOUBLE PRECISION, DIMENSION(N) :: s_array // mismatch of N and s_N, the same SgArrayType is reused.
      //new_param_name= "s_"+new_param_name; //s_ means shared variables
      new_param_name= new_param_name; //s_ means shared variables
    }

  }

  // Fortran parameters are passed by reference by default,
  // So use base type directly
  // C/C++ parameters will use their new param type to implement pass-by-reference
  if (SageInterface::is_Fortran_language())
    return OutlinedFuncParam_t (new_param_name,param_base_type);
  else 
    return OutlinedFuncParam_t (new_param_name, new_param_type);
}

/*!
 *  \brief Initializes unpacking statements for array types
 *  The function takes into account that array types must be initialized element by element
 *  The function also skips typedef types to get the real type
 *  
 *  \param lhs Left-hand side of the assignment 
 *  \param rhs Right-hand side of the assignment
 *  \param type Current type being initialized
 *  \param scope Scope where the assignments will be placed
 *  \param loop_indexes Indexes of all loops, to be declared after calling this function
 *                      So they are initialized before the most outer loop
 *
 *  Example:
 *    Outlined parameters struct:
 *        struct OUT__1__7768___data {
 *            void *a_p;
 *            int (*b_p)[10UL];
 *            int c[10UL];
 *            void *d_p;
 *        };
 *    Unpacking statements:
 *        int *a = (int *)(((struct OUT__1__7768___data *)__out_argv) -> a_p);                      -> shared scalar
 *        int (*b)[10UL] = (int (*)[10UL])(((struct OUT__1__7768___data *)__out_argv) -> b_p);      -> shared static array
 *        int __i0__;
 *        for (__i0__ = 0; __i0__ < 10UL; __i0__++)                                                 -> firstprivate array 
 *            c[__i0__] = ((struct OUT__1__7768___data *)__out_argv) -> c[__i0__];
 *        int **d = (int **)(((struct OUT__1__7768___data *)__out_argv) -> d_p);                    -> shared dynamic array
 */
static SgStatement* build_array_unpacking_statement( SgExpression * lhs, SgExpression * rhs, SgType * type, 
                                                     SgScopeStatement * scope, SgStatementPtrList & loop_indexes )
{
    ROSE_ASSERT( isSgArrayType( type ) );
    ROSE_ASSERT( scope );
    
    // Loop initializer
    std::string loop_index_name = SageInterface::generateUniqueVariableName( scope, "i" );
    SgVariableDeclaration * loop_index = buildVariableDeclaration( loop_index_name, buildIntType( ), NULL /* initializer */, scope );
    loop_indexes.push_back( loop_index );
    SgStatement * loop_init = buildAssignStatement( buildVarRefExp( loop_index_name, scope ), buildIntVal( 0 ) );
    
    // Loop test
    SgStatement * loop_test = buildExprStatement(
            buildLessThanOp( buildVarRefExp( loop_index_name, scope ), isSgArrayType( type )->get_index( ) ) );
    
    // Loop increment
    SgExpression * loop_increment = buildPlusPlusOp( buildVarRefExp( loop_index_name, scope ), SgUnaryOp::postfix );
    
    // Loop body    
    SgExpression * assign_lhs = buildPntrArrRefExp( lhs, buildVarRefExp( loop_index_name, scope ) );
    SgExpression * assign_rhs = buildPntrArrRefExp( rhs, buildVarRefExp( loop_index_name, scope ) );
    SgStatement * loop_body = NULL;
    SgType * base_type = isSgArrayType( type )->get_base_type( )->stripType( SgType::STRIP_TYPEDEF_TYPE );
    if( isSgArrayType( base_type ) )
    {
        loop_body = build_array_unpacking_statement( assign_lhs, assign_rhs, base_type, scope, loop_indexes );
    }
    else
    {
        loop_body = buildAssignStatement( assign_lhs, assign_rhs );
    }
    
    // Loop satement
    return buildForStatement( loop_init, loop_test, loop_increment, loop_body );
}

/*!
 *  \brief Creates a local variable declaration to "unpack" an outlined-function's parameter
 *  int index is optionally used as an offset inside a wrapper parameter for multiple variables
 *
 *  The key is to set local_name, local_type, and local_val for all cases
 *
 *  There are three choices:
 *  Case 1: unpack one variable from one parameter
 *  -----------------------------------------------
 *  OUT_XXX(int *ip__)
 *  {
 *    // This is called unpacking declaration for a read-only variable, Liao, 9/11/2008
 *   int i = * (int *) ip__;
 *  }
 *
 *  Case 2: unpack one variable from an array of pointers
 *  -----------------------------------------------
 *  OUT_XXX (void * __out_argv[n]) // for written variables, we have to use pointers
 *  {
 *    int * _p_i =  (int*)__out_argv[0];
 *    int * _p_j =  (int*)__out_argv[1];
 *    ....
 *  }
 *
 *  Case 3: unpack one variable from a structure
 *  -----------------------------------------------
 *  OUT__xxx (struct OUT__xxx__data *__out_argv)
 *  {
 *    int i = __out_argv->i;
 *    int j = __out_argv->j;
 *    int (*sum)[100UL] = __out_argv->sum_p;*
 *  }
 *
 * case 1 and case 2 have two variants: 
 *   using conventional pointer dereferencing or 
 *   using cloned variables(temp_variable)
 *
 */
static
SgVariableDeclaration *
createUnpackDecl (SgInitializedName* param, // the function parameter
                  int index, // the index to the array of pointers type
                  bool isPointerDeref, // must use pointer deference or not
                  const SgInitializedName* i_name, // original variable to be passed as the function parameter
                  SgClassDeclaration* struct_decl, // the struct declaration type used to wrap parameters 
                  SgScopeStatement* scope) // the scope into which the statement will be inserted
{
    ROSE_ASSERT( param && scope && i_name );
    
    // keep the original name 
    const string orig_var_name = i_name->get_name( ).str( );

    //---------------step 1 -----------------------------------------------
    // decide on the type : local_type
    // the original data type of the variable passed via parameter
    SgType* orig_var_type = i_name ->get_type();
    bool is_array_parameter = false;
    if( !SageInterface::is_Fortran_language( ) )
    {  
        // Convert an array type parameter's first dimension to a pointer type
        // This conversion is implicit for C/C++ language. 
        // We have to make it explicit to get the right type
        // Liao, 4/24/2009  TODO we should only adjust this for the case 1
        if( isSgArrayType(orig_var_type) ) 
            if( isSgFunctionDefinition( i_name->get_scope( ) ) )
            {    
                orig_var_type = SageBuilder::buildPointerType(isSgArrayType(orig_var_type)->get_base_type());
                is_array_parameter = true;
            }
    }

    SgType* local_type = NULL;
    if( SageInterface::is_Fortran_language( ) )
        local_type= orig_var_type;
    else if( Outliner::temp_variable || Outliner::useStructureWrapper ) 
    // unique processing for C/C++ if temp variables are used
    {
        if( isPointerDeref || ( !isPointerDeref && is_array_parameter ) )    
            // use pointer dereferencing for some
            local_type = buildPointerType(orig_var_type);
        else                    // use variable clone instead for others
            local_type = orig_var_type;
    }  
    else // all other cases: non-fortran, not using variable clones 
    {
        if( is_C_language( ) )
        {   
            // we use pointer types for all variables to be passed
            // the classic outlining will not use unpacking statement, but use the parameters directly.
            // So we can safely always use pointer dereferences here
            local_type = buildPointerType( orig_var_type );
        }
        else // C++ language
            // Rich's idea was to leverage C++'s reference type: two cases:
            //  a) for variables of reference type: no additional work
            //  b) for others: make a reference type to them
            //   all variable accesses in the outlined function will have
            //   access the address of the by default, not variable substitution is needed 
        { 
            local_type = isSgReferenceType( orig_var_type ) ? orig_var_type 
                                                            : SgReferenceType::createType( orig_var_type );
        }
    }

    ROSE_ASSERT( local_type );

    SgAssignInitializer* local_val = NULL;
    
    // Declare a local variable to store the dereferenced argument.
    SgName local_name( orig_var_name.c_str( ) );
    if( SageInterface::is_Fortran_language( ) )
        local_name = SgName( param->get_name( ) );
    
    // This is the right hand of the assignment we want to build
    //
    // ----------step  2. Create the right hand expression------------------------------------
    // No need to have right hand for fortran
    if( SageInterface::is_Fortran_language( ) )
    {
        local_val = NULL;
        return buildVariableDeclaration( local_name, local_type, local_val, scope );
    }
    // for non-fortran language
    // Create an expression that "unpacks" the parameter.
    //   case 1: default is to use the parameter directly
    //   case 2:  for array of pointer type parameter,  build an array element reference
    //   case 3: for structure type parameter, build a field reference
    //     we use type casting to have generic void * parameter and void* data structure fields
    //     so less types are to exposed during translation
    //   class Hello **this__ptr__ = (class Hello **)(((struct OUT__1__1527___data *)__out_argv) -> this__ptr___p);
    SgExpression* param_ref = NULL;
    if (Outliner::useParameterWrapper) // using index for a wrapper parameter
    {
        if( Outliner::useStructureWrapper )
        {   // case 3: structure type parameter
            if (struct_decl != NULL)
            {
                SgClassDefinition* struct_def = isSgClassDeclaration( struct_decl->get_definingDeclaration( ) )->get_definition( );
                ROSE_ASSERT( struct_def != NULL );
                string field_name = orig_var_name;
                if( isPointerDeref )
                    field_name = field_name + "_p";
                // __out_argv->i  or __out_argv->sum_p , depending on if pointer deference is needed
                // param_ref = buildArrowExp(buildVarRefExp(param, scope), buildVarRefExp(field_name, struct_def));
                // We use void* for all pointer types elements within the data structure. So type casting is needed here
                // e.g.   class Hello **this__ptr__ = (class Hello **)(((struct OUT__1__1527___data *)__out_argv) -> this__ptr___p);
                

                param_ref = buildArrowExp( buildCastExp( buildVarRefExp( param, scope ), 
                                                         buildPointerType( struct_decl->get_type( ) ) ), 
                                           buildVarRefExp( field_name, struct_def ) );
                if( !isSgArrayType( local_type ) )
                {
                    // When necessary, we must catch the address before we do the casting
                    if( !isPointerDeref && is_array_parameter )
                    {
                        param_ref = buildAddressOfOp( param_ref );
                    }
                    
                    param_ref = buildCastExp( param_ref, local_type );
                }
            }
            else
            {
                cerr << "Outliner::createUnpackDecl(): no need to unpack anything since struct_decl is NULL." << endl;
                ROSE_ASSERT( false );
            }
        }
        else // case 2: array of pointers 
        {
            param_ref = buildPntrArrRefExp( buildVarRefExp( param, scope ), buildIntVal( index ) );
        }
    } 
    else // default case 1:  each variable has a pointer typed parameter , 
         // this is not necessary but we have a classic model for optimizing this
    {
        param_ref = buildVarRefExp( param, scope );
    }

    ROSE_ASSERT (param_ref != NULL); 

    if (Outliner::useStructureWrapper)
    {
        // Or for structure type paramter
        // int (*sum)[100UL] = __out_argv->sum_p; // is PointerDeref type
        // int i = __out_argv->i;
            local_val = buildAssignInitializer( param_ref ); 
    }
    else
    {
        //TODO: This is only needed for case 2 or C++ case 1, 
        // not for case 1 and case 3 since the source right hand already has the right type
        // since the array has generic void* elements. We need to cast from 'void *' to 'LOCAL_VAR_TYPE *'
        // Special handling for C++ reference type: addressOf (refType) == addressOf(baseType) 
        // So unpacking it to baseType* 
        SgReferenceType* ref = isSgReferenceType (orig_var_type);
        SgType* local_var_type_ptr =  SgPointerType::createType (ref ? ref->get_base_type (): orig_var_type);
        ROSE_ASSERT (local_var_type_ptr);
        SgCastExp* cast_expr = buildCastExp(param_ref,local_var_type_ptr,SgCastExp::e_C_style_cast);

        if (Outliner::temp_variable) // variable cloning is enabled
        {
            // int* ip = (int *)(__out_argv[1]); // isPointerDeref == true
            // int i = *(int *)(__out_argv[1]);
            if (isPointerDeref)
            {
                local_val = buildAssignInitializer(cast_expr); // casting is enough for pointer types
            }
            else // temp variable need additional dereferencing from the parameter on the right side
            {
                local_val = buildAssignInitializer(buildPointerDerefExp(cast_expr));
            }
        } 
        else // conventional pointer dereferencing algorithm
        {
            // int* ip = (int *)(__out_argv[1]);
            if  (is_C_language()) // using pointer dereferences
            {
                local_val = buildAssignInitializer(cast_expr);
            }
            else if  (is_Cxx_language()) 
            // We use reference type in the outlined function's body for C++ 
            // need the original value from a dereferenced type
            // using pointer dereferences to get the original type
            //  we use reference type instead of pointer type for C++
            /*
             * extern "C" void OUT__1__8452__(int *ip__,int *jp__,int (*sump__)[100UL]) {
             *   int &i =  *((int *)ip__);
             *   int &j =  *((int *)jp__);
             *   int (&sum)[100UL] =  *((int (*)[100UL])sump__);
             *   ...
             * };
             */ 
            {
                local_val = buildAssignInitializer(buildPointerDerefExp(cast_expr));
            }
            else
            {
                printf ("No other languages are supported by outlining currently. \n");
                ROSE_ASSERT(false);
            }
        }
    }

    SgVariableDeclaration* decl;
    if( isSgArrayType( local_type->stripType( SgType::STRIP_TYPEDEF_TYPE ) ) )
    {   // The original variable was no statically allocated and passed as private or firstprivate
        // We need to copy every element of the array
        decl = buildVariableDeclaration( local_name, local_type, NULL, scope );
        SgStatementPtrList loop_indexes;
        SgStatement * array_init = build_array_unpacking_statement( buildVarRefExp( decl ), param_ref, 
                                                                    local_type->stripType( SgType::STRIP_TYPEDEF_TYPE ), scope, loop_indexes );
        SageInterface::prependStatement( array_init, scope );
        SageInterface::prependStatementList( loop_indexes, scope );
    }
    else
    {
        decl = buildVariableDeclaration( local_name, local_type, local_val, scope );
    }
    return decl;
}

//! Returns 'true' if the given type is 'const'.
static
bool
isReadOnlyType (const SgType* type)
{
  ROSE_ASSERT (type);

  const SgModifierType* mod = 0;
  switch (type->variantT ())
    {
    case V_SgModifierType:
      mod = isSgModifierType (type);
      break;
    case V_SgReferenceType:
      mod = isSgModifierType (isSgReferenceType (type)->get_base_type ());
      break;
    case V_SgPointerType:
      mod = isSgModifierType (isSgPointerType (type)->get_base_type ());
      break;
    default:
      mod = 0;
      break;
    }
  return mod
    && mod->get_typeModifier ().get_constVolatileModifier ().isConst ();
}

/*!
 *  \brief Creates an assignment to "pack" a local variable back into
 *  an outlined-function parameter that has been passed as a pointer
 *  value.
 *  Only applicable when variable cloning is turned on.
 *
 *  The concept of pack/unpack is associated with parameter wrapping
 *  In the outlined function, we first
 *    unpack the wrapper parameter to get individual parameters
 *  then
 *    pack individual parameter into the wrapper. 
 *  
 *  It is also write-back or transfer-back the values of clones to their original pointer variables
 *
 *  This routine takes the original "unpack" definition, of the form
 *
 *    TYPE local_unpack_var = *outlined_func_arg; // no parameter wrapping
 *    int i = *(int *)(__out_argv[1]); // parameter wrapping case
 *
 *  and creates the "re-pack" assignment expression,
 *
 *    *outlined_func_arg = local_unpack_var // no-parameter wrapping case
 *    *(int *)(__out_argv[1]) =i; // parameter wrapping case
 *
 *  C++ variables of reference types do not need this step.
 */
static
SgAssignOp *
createPackExpr (SgInitializedName* local_unpack_def)
{
  if (!Outliner::temp_variable)
  {
    if (is_C_language()) //skip for pointer dereferencing used in C language
      return NULL;
  }
  // reference types do not need copy the value back in any cases
  if (isSgReferenceType (local_unpack_def->get_type ()))  
    return NULL;
  
  // Liao 10/26/2009, Most of time, using data structure of parameters don't need copy 
  // a local variable's value back to its original parameter
  if (Outliner::useStructureWrapper)
  {
    if (is_Cxx_language())
      return NULL;
  }

  // We expect that the value transferring back to the original parameter is only 
  // needed for variable clone options and when the variable is being written. 
  if (local_unpack_def
      && !isReadOnlyType (local_unpack_def->get_type ()))
//      && !isSgReferenceType (local_unpack_def->get_type ()))
    {
      SgName local_var_name (local_unpack_def->get_name ());

      SgAssignInitializer* local_var_init =
        isSgAssignInitializer (local_unpack_def->get_initializer ());
      ROSE_ASSERT (local_var_init);

      // Create the LHS, which derefs the function argument, by
      // copying the original dereference expression.
      // 
      SgPointerDerefExp* param_deref_unpack =
        isSgPointerDerefExp (local_var_init->get_operand_i ());
      if (param_deref_unpack == NULL)  
      {
        cout<<"packing statement is:"<<local_unpack_def->get_declaration()->unparseToString()<<endl;
        cout<<"local unpacking stmt's initializer's operand has non-pointer dereferencing type:"<<local_var_init->get_operand_i ()->class_name()<<endl;
        ROSE_ASSERT (param_deref_unpack);
      }

      SgPointerDerefExp* param_deref_pack = isSgPointerDerefExp (ASTtools::deepCopy (param_deref_unpack));
      ROSE_ASSERT (param_deref_pack);
              
      // Create the RHS, which references the local variable.
      SgScopeStatement* scope = local_unpack_def->get_scope ();
      ROSE_ASSERT (scope);
      SgVariableSymbol* local_var_sym =
        scope->lookup_var_symbol (local_var_name);
      ROSE_ASSERT (local_var_sym);
      SgVarRefExp* local_var_ref = SageBuilder::buildVarRefExp (local_var_sym);
      ROSE_ASSERT (local_var_ref);

      // Assemble the final assignment expression.
      return SageBuilder::buildAssignOp (param_deref_pack, local_var_ref);
    }
  return 0;
}

/*!
 *  \brief Creates a pack (write-back) statement , used to support variable cloning in outlining.
 *
 *  
 *  This routine creates an SgExprStatement wrapper around the return
 *  of createPackExpr.
 *  
 *  void OUT__1__4305__(int *ip__,int *sump__)
 * {
 *   // variable clones for pointer types
 *   int i =  *((int *)ip__);
 *   int sum =  *((int *)sump__);
 *
 *  // clones participate computation
 *   for (i = 0; i < 100; i++) {
 *     sum += i;
 *   }
 *  // write back the values from clones to their original pointers 
 *  //The following are called (re)pack statements
 *    *((int *)sump__) = sum;
 *    *((int *)ip__) = i;
}

 */
static
SgExprStatement *
createPackStmt (SgInitializedName* local_unpack_def)
{
  // No repacking for Fortran for now
  if (local_unpack_def==NULL || SageInterface::is_Fortran_language())
    return NULL;
  SgAssignOp* pack_expr = createPackExpr (local_unpack_def);
  if (pack_expr)
    return SageBuilder::buildExprStatement (pack_expr);
  else
    return 0;
}


/*!
 *  \brief Records a mapping between two variable symbols, and record
 *  the new symbol.
 *
 *  This routine creates the target variable symbol from the specified
 *  SgInitializedName object. If the optional scope is specified
 *  (i.e., is non-NULL), then this routine also inserts the new
 *  variable symbol into the scope's symbol table.
 */
static
void
recordSymRemap (const SgVariableSymbol* orig_sym,
                SgInitializedName* name_new,
                SgScopeStatement* scope,
                VarSymRemap_t& sym_remap)
{
  if (orig_sym && name_new)
    { //TODO use the existing symbol associated with name_new!
   // DQ (2/24/2009): Added assertion.
      ROSE_ASSERT(name_new->get_name().is_null() == false);

      SgVariableSymbol* sym_new = new SgVariableSymbol (name_new);
      ROSE_ASSERT (sym_new);
      sym_remap.insert (VarSymRemap_t::value_type (orig_sym, sym_new));

      if (scope)
        {
          scope->insert_symbol (name_new->get_name (), sym_new);
          name_new->set_scope (scope);
        }
    }
}

/*!
 *  \brief Records a mapping between variable symbols.
 *
 *  \pre The variable declaration must contain only 1 initialized
 *  name.
 */
static
void
recordSymRemap (const SgVariableSymbol* orig_sym,
                SgVariableDeclaration* new_decl,
                SgScopeStatement* scope,
                VarSymRemap_t& sym_remap)
{
  if (orig_sym && new_decl)
    {
      SgInitializedNamePtrList& vars = new_decl->get_variables ();
      ROSE_ASSERT (vars.size () == 1);
      for (SgInitializedNamePtrList::iterator i = vars.begin ();
           i != vars.end (); ++i)
        recordSymRemap (orig_sym, *i, scope, sym_remap);
    }
}

// Handle OpenMP private variables: variables to be declared and used within the outlined function
// Input: 
//     pSyms: private variable set provided by caller functions
//     scope: the scope of a private variable's local declaration
// Output:    
//    private_remap: a map between the original variables and their private copies
// 
// Internal: for each private variable, 
//    create a local declaration of the same name, 
//    record variable mapping to be used for replacement later on
#if 0    
static void handlePrivateVariables( const ASTtools::VarSymSet_t& pSyms,
                                    SgScopeStatement* scope, 
                                    VarSymRemap_t& private_remap)
{
  // --------------------------------------------------
  for (ASTtools::VarSymSet_t::const_reverse_iterator i = pSyms.rbegin ();
      i != pSyms.rend (); ++i)
  {
    const SgInitializedName* i_name = (*i)->get_declaration ();
    ROSE_ASSERT (i_name);
    string name_str = i_name->get_name ().str ();
    SgType * v_type = i_name->get_type();
    SgVariableDeclaration* local_var_decl = buildVariableDeclaration(name_str, v_type, NULL, scope);
    prependStatement (local_var_decl,scope);
    recordSymRemap (*i, local_var_decl, scope, private_remap);
  }
}

#endif

// Create one parameter for an outlined function
// classic_original_type flag is used to decide the parameter type: 
//   A simplest case: readonly -> pass-by-value -> same type v.s. written -> pass-by-reference -> pointer type
// return the created parameter
SgInitializedName* createOneFunctionParameter(const SgInitializedName* i_name, 
                              bool classic_original_type, // control if the original type should be used, instead of a pointer type, only used with enable_classic flag for now
                             SgFunctionDeclaration* func)
{
  ROSE_ASSERT (i_name);

  ROSE_ASSERT (func);
  SgFunctionParameterList* params = func->get_parameterList ();
  ROSE_ASSERT (params);
  SgFunctionDefinition* def = func->get_definition ();
  ROSE_ASSERT (def);

  // It handles language-specific details internally, like pass-by-value, pass-by-reference
  // name and type is not enough, need the SgInitializedName also for tell 
  // if an array comes from a parameter list
  OutlinedFuncParam_t param = createParam (i_name,classic_original_type);
  SgName p_sg_name (param.first.c_str ());
  // name, type, declaration, scope, 
  // TODO function definition's declaration should not be passed to createInitName()
  SgInitializedName* p_init_name = createInitName (param.first, param.second, def->get_declaration(), def);
  ROSE_ASSERT (p_init_name);
  prependArg(params,p_init_name);
  return p_init_name;
}

// ===========================================================
//! Fixes up references in a block to point to alternative symbols.
// based on an existing symbol-to-symbol map
// Also called variable substitution or variable replacement
static void
remapVarSyms (const VarSymRemap_t& vsym_remap,  // regular shared variables
              const ASTtools::VarSymSet_t& pdSyms, // variables which must use pointer dereferencing somehow. //special shared variables using variable cloning (temp_variable)
              const VarSymRemap_t& private_remap,  // variables using private copies
              SgBasicBlock* b)
{
  // Check if variable remapping is even needed.
  if (vsym_remap.empty() && private_remap.empty())
    return;
  // Find all variable references
  typedef Rose_STL_Container<SgNode *> NodeList_t;
  NodeList_t refs = NodeQuery::querySubTree (b, V_SgVarRefExp);
  // For each of the references , 
  for (NodeList_t::iterator i = refs.begin (); i != refs.end (); ++i)
  {
    // Reference possibly in need of fix-up.
    SgVarRefExp* ref_orig = isSgVarRefExp (*i);
    ROSE_ASSERT (ref_orig);

    // Search for a symbol which need to be replaced.
    VarSymRemap_t::const_iterator ref_new =  vsym_remap.find (ref_orig->get_symbol ());
    VarSymRemap_t::const_iterator ref_private =  private_remap.find (ref_orig->get_symbol ());

    // a variable could be both a variable needing passing original value and private variable 
    // such as OpenMP firstprivate, lastprivate and reduction variable
    // For variable substitution, private remap has higher priority 
    // remapping private variables
    if (ref_private != private_remap.end()) 
    {
      // get the replacement variable
      SgVariableSymbol* sym_new = ref_private->second;
      // Do the replacement
      ref_orig->set_symbol (sym_new);
    }
    else if (ref_new != vsym_remap.end ()) // Needs replacement, regular shared variables
    {
      SgVariableSymbol* sym_new = ref_new->second;
      if (Outliner::temp_variable || Outliner::useStructureWrapper)
        // uniform handling if temp variables of the same type are used
      {// two cases: variable using temp vs. variables using pointer dereferencing!

        if (pdSyms.find(ref_orig->get_symbol())==pdSyms.end()) //using temp
          ref_orig->set_symbol(sym_new);
        else
        {
          SgPointerDerefExp * deref_exp = SageBuilder::buildPointerDerefExp(buildVarRefExp(sym_new));
          // deref_exp->set_need_paren(true);       // This is done again inside SageInterface::replaceExpression
          SageInterface::replaceExpression(isSgExpression(ref_orig),isSgExpression(deref_exp));
        }
      }
      else // no variable cloning is used
      {
        if (is_C_language()) 
          // old method of using pointer dereferencing indiscriminately for C input
          // TODO compare the orig and new type, use pointer dereferencing only when necessary
        {
          SgPointerDerefExp * deref_exp = SageBuilder::buildPointerDerefExp(buildVarRefExp(sym_new));
          deref_exp->set_need_paren(true);
          SageInterface::replaceExpression(isSgExpression(ref_orig),isSgExpression(deref_exp));
        }
        else
          ref_orig->set_symbol (sym_new);
      }
    } //find an entry
  } // for every refs

}


/*!
 *  \brief Creates new function parameters for a set of variable symbols.
 *
 *  We have several options for the organization of function parameters:
 *
 *  1. default: each variable to be passed has a function parameter
 *           To support both C and C++ programs, this routine assumes parameters passed
 *           using pointers (rather than the C++ -specific reference types).  
 *  2, useParameterWrapper: use an array as the function parameter, each
 *              pointer stores the address of the variable to be passed
 *  3. useStructureWrapper: use a structure, each field stores a variable's
 *              value or address according to use-by-address or not semantics
 *
 *  It inserts "unpacking/unwrapping" and "repacking" statements at the 
 *  beginning and end of the function body, respectively, when necessary.
 *
 *  This routine records the mapping between the given variable symbols and the new
 *  symbols corresponding to the new parameters. 
 *
 *  Finally, it performs variable replacement in the end.
 *
 */
static
std::set<SgVariableDeclaration *>
variableHandling(const ASTtools::VarSymSet_t& syms, // all variables passed to the outlined function: //regular (shared) parameters?
              const ASTtools::VarSymSet_t& pdSyms, // those must use pointer dereference: use pass-by-reference
//              const std::set<SgInitializedName*> & readOnlyVars, // optional analysis: those which can use pass-by-value, used for classic outlining without parameter wrapping, and also for variable clone to decide on if write-back is needed
//              const std::set<SgInitializedName*> & liveOutVars, // optional analysis: used to control if a write-back is needed when variable cloning is used.
              const std::set<SgInitializedName*> & restoreVars, // variables to be restored after variable cloning
              SgClassDeclaration* struct_decl, // an optional struct wrapper for all variables
              SgFunctionDeclaration* func) // the outlined function
{
  VarSymRemap_t sym_remap; // variable remapping for regular(shared) variables: all passed by reference using pointer types?
  VarSymRemap_t private_remap; // variable remapping for private/firstprivate/reduction variables
  ROSE_ASSERT (func);
  SgFunctionParameterList* params = func->get_parameterList ();
  ROSE_ASSERT (params);
  SgFunctionDefinition* def = func->get_definition ();
  ROSE_ASSERT (def);
  SgBasicBlock* body = def->get_body ();
  ROSE_ASSERT (body);

  // Place in which to put new outlined variable symbols.
  SgScopeStatement* args_scope = isSgScopeStatement (body);
  ROSE_ASSERT (args_scope);

  // For each variable symbol, create an equivalent function parameter. 
  // Also create unpacking and repacking statements.
  int counter=0;
  SgInitializedName* parameter1=NULL; // the wrapper parameter
  SgVariableDeclaration*  local_var_decl  =  NULL;

  // handle OpenMP private variables/ or those which are neither live-in or live-out
//  handlePrivateVariables(pSyms, body, private_remap);
//  This is done before calling the outliner now, by transOmpVariables()

  // --------------------------------------------------
  // for each parameters passed to the outlined function
  // They include parameters for 
  // *  regular shared variables and also 
  // *  shared copies for firstprivate and reduction variables
  std::set<SgVariableDeclaration *> unpacking_stmts;
  for (ASTtools::VarSymSet_t::const_reverse_iterator i = syms.rbegin ();
      i != syms.rend (); ++i)
  {
    // Basic information about the variable to be passed into the outlined function
    // Variable symbol name
    const SgInitializedName* i_name = (*i)->get_declaration ();
    ROSE_ASSERT (i_name);
    string name_str = i_name->get_name ().str ();
    SgName p_sg_name (name_str);
    const SgVariableSymbol * sym = isSgVariableSymbol(*i);

    //SgType* i_type = i_name->get_type ();
//    bool readOnly = false;
    bool use_orig_type = false;
//    if (readOnlyVars.find(const_cast<SgInitializedName*> (i_name)) != readOnlyVars.end())
//      readOnly = true;
    if (pdSyms.find(sym) == pdSyms.end()) // not a variable to use AddressOf, then it should be a variable using its original type
       use_orig_type = true;
    // step 1. Create parameters and insert it into the parameter list of the outlined function.
    // ----------------------------------------
    SgInitializedName* p_init_name = NULL;
    // Case 1: using a wrapper for all variables 
    //   two choices: array of pointers (default)  vs. structure 
    if (!Outliner::enable_classic && Outliner::useParameterWrapper) // Liao 3/26/2013. enable_classic overrules useParameterWrapper
 //   if (Outliner::useParameterWrapper)
    {
      if (i==syms.rbegin())
      {
        SgName var1_name = "__out_argv";
        // This is needed to support the pass-by-value semantics across different thread local stacks
        // In this situation, pointer dereferencing cannot be used to get the value 
        // of an inactive parent thread's local variables
        SgType* ptype= NULL; 
        if (Outliner::useStructureWrapper)
        {
          // To have strict type matching in C++ model
          // between the outlined function and the function pointer passed to the gomp runtime lib
          // we use void* for the parameter type
          #if 0 
          if (struct_decl != NULL)
            ptype = buildPointerType (struct_decl->get_type());
          else
          #endif  
            ptype = buildPointerType (buildVoidType());
        }
        else // use array of pointers, regardless of the pass-by-value vs. pass-by-reference difference
          ptype= buildPointerType(buildPointerType(buildVoidType()));

        parameter1 = buildInitializedName(var1_name,ptype);
        appendArg(params,parameter1);
      }
      p_init_name = parameter1; // set the source parameter to the wrapper
    }
    else // case 3: use a parameter for each variable, the default case and the classic case
       p_init_name = createOneFunctionParameter(i_name, use_orig_type , func); 

    // step 2. Create unpacking/unwrapping statements, also record variables to be replaced
    // ----------------------------------------
    bool isPointerDeref = false; 
    if (Outliner::temp_variable || Outliner::useStructureWrapper)  //TODO add enable_classic flag here? no since there is no need to unpack parameter in the classic behavior, for default outlining, all variables are passed by references anyway, so no use neither
    { // Check if the current variable belongs to the symbol set 
      //suitable for using pointer dereferencing
      const SgVariableSymbol* i_sym = isSgVariableSymbol(i_name->get_symbol_from_symbol_table ());
      ROSE_ASSERT(i_sym!=NULL);
      if ( pdSyms.find(i_sym)!=pdSyms.end())
        isPointerDeref = true;
    }  

    if (Outliner::enable_classic) 
    // classic methods use parameters directly, no unpacking is needed
    {
      if (!use_orig_type) 
      //read only variable should not have local variable declaration, using parameter directly
      // taking advantage of the same parameter names for readOnly variables
      //
      // Let postprocessing to patch up symbols for them
      {
        // non-readonly variables need to be mapped to their parameters with different names (p__)
        // remapVarSyms() will use pointer dereferencing for all of them by default in C, 
        // this is enough to mimic the classic outlining work 
        recordSymRemap(*i,p_init_name, args_scope, sym_remap); 
      }
    } else 
    { // create unwrapping statements from parameters/ or the array parameter for pointers
      //if (SageInterface::is_Fortran_language())
      //  args_scope = NULL; // not sure about Fortran scope
      
       // Not true: even without parameter wrapping, we still need to transfer the function parameter to a local declaration, which is also called unpacking
      // must be a case of using parameter wrapping
      // ROSE_ASSERT (Outliner::useStructureWrapper || Outliner::useParameterWrapper);
      local_var_decl  = 
        createUnpackDecl (p_init_name, counter, isPointerDeref, i_name , struct_decl, body);
      ROSE_ASSERT (local_var_decl);
      unpacking_stmts.insert( local_var_decl );
      prependStatement (local_var_decl,body);
      // regular and shared variables used the first local declaration
      recordSymRemap (*i, local_var_decl, args_scope, sym_remap);
      // transfer the value for firstprivate variables. 
      // TODO
    }

    // step 3. Create and insert companion re-pack statement in the end of the function body
    // ----------------------------------------
    SgInitializedName* local_var_init = NULL;
    if (local_var_decl != NULL )
      local_var_init = local_var_decl->get_decl_item (SgName (name_str.c_str ()));

    if (!SageInterface::is_Fortran_language() && !Outliner::enable_classic)  
      ROSE_ASSERT(local_var_init!=NULL);  

    // Only generate restoring statement for non-pointer dereferencing cases
    // if temp variable mode is enabled
    if (Outliner::temp_variable)
    {
      if(!isPointerDeref)
      {
#if 0        
        //conservatively consider them as all live out if no liveness analysis is enabled,
        bool isLiveOut = true;
        if (Outliner::enable_liveness)
          if (liveOutVars.find(const_cast<SgInitializedName*> (i_name))==liveOutVars.end())
            isLiveOut = false;

        // generate restoring statements for written and liveOut variables:
        //  isWritten && isLiveOut --> !isRead && isLiveOut --> (findRead==NULL && findLiveOut!=NULL)
        // must compare to the original init name (i_name), not the local copy (local_var_init)
        if (readOnlyVars.find(const_cast<SgInitializedName*> (i_name))==readOnlyVars.end() && isLiveOut)   // variables not in read-only set have to be restored
#endif          
        if (restoreVars.find(const_cast<SgInitializedName*> (i_name))!=restoreVars.end())
        {
          if (Outliner::enable_debug && local_var_init != NULL)
            cout<<"Generating restoring statement for non-read-only variable:"<<local_var_init->unparseToString()<<endl;

          SgExprStatement* pack_stmt = createPackStmt (local_var_init);
          if (pack_stmt)
            appendStatement (pack_stmt,body);
        }
        else
        {
          if (Outliner::enable_debug && local_var_init != NULL)
            cout<<"skipping a read-only variable for restoring its value:"<<local_var_init->unparseToString()<<endl;
        }
      } else
      {
        if (Outliner::enable_debug && local_var_init != NULL)
          cout<<"skipping a variable using pointer-dereferencing for restoring its value:"<<local_var_init->unparseToString()<<endl;
      }
    }
    else
    { 
      // TODO: why do we have this packing statement at all if no variable cloning is used??
      SgExprStatement* pack_stmt = createPackStmt (local_var_init);
      if (pack_stmt)
      {
        appendStatement (pack_stmt,body);
        cerr<<"Error: createPackStmt() is called while Outliner::temp_variable is false!"<<endl;
        ROSE_ASSERT (false); 
      }
    }
    counter ++;
  } //end for

  SgBasicBlock* func_body = func->get_definition()->get_body();

#if 1
  //TODO: move this outside of outliner since it is OpenMP-specific. omp_lowering.cpp generateOutlinedTask()
  // A caveat is the moving this also means we have to patch up prototype later
  //For OpenMP lowering, we have to have a void * parameter even if there is no need to pass any parameters 
  //in order to match the gomp runtime lib 's function prototype for function pointers
  SgFile* cur_file = getEnclosingFileNode(func);
  ROSE_ASSERT (cur_file != NULL);
  //if (cur_file->get_openmp_lowering () && ! SageInterface::is_Fortran_language())
  if (cur_file->get_openmp_lowering ())
  {
    if (syms.size() ==0)
    {
      SgName var1_name = "__out_argv";
      SgType* ptype= NULL; 
      // A dummy integer parameter for Fortran outlined function
      if (SageInterface::is_Fortran_language() )
      {
        var1_name = "out_argv";
        ptype = buildIntType();
        SgVariableDeclaration *var_decl = buildVariableDeclaration(var1_name,ptype, NULL, func_body);
        prependStatement(var_decl, func_body);

      }
      else
      {
        ptype = buildPointerType (buildVoidType());
        ROSE_ASSERT (Outliner::useStructureWrapper); //TODO: this assertion may no longer true for "omp target" + "omp parallel for", in which map() may not show up at all (0 variables)
      }
      parameter1 = buildInitializedName(var1_name,ptype);
      appendArg(params,parameter1);
    }
  }
#endif
  // variable substitution 
  remapVarSyms (sym_remap, pdSyms, private_remap , func_body);
  
  return unpacking_stmts;
}

// =====================================================================

// DQ (2/25/2009): Modified function interface to pass "SgBasicBlock*" as not const parameter.
//! Create a function named 'func_name_str', with a parameter list from 'syms'
SgFunctionDeclaration *
Outliner::generateFunction ( SgBasicBlock* s,  // block to be outlined
                            const string& func_name_str, // function name provided
                            const ASTtools::VarSymSet_t& syms, // variables to be passed in/out the outlined function
                            const ASTtools::VarSymSet_t& pdSyms, // variables to be passed using its address. using pointer dereferencing (AddressOf() for pass-by-reference), most use for struct wrapper
//                          const std::set<SgInitializedName*>& readOnlyVars, // optional readOnly variables to guide classic outlining's parameter handling and variable cloning's write-back generation
//                          const std::set< SgInitializedName *>& liveOuts, // optional live out variables, used to optimize variable cloning
                            const std::set< SgInitializedName *>& restoreVars, // optional information about variables to be restored after variable clones finish computation
                            SgClassDeclaration* struct_decl,  // an optional wrapper structure for parameters
                            SgScopeStatement* scope,
                            std::set<SgVariableDeclaration *>& unpack_stmts )
{
  ROSE_ASSERT (s&&scope);
  ROSE_ASSERT(isSgGlobal(scope));
#if 0  
  // step 1: perform necessary liveness and side effect analysis, if requested.
  // This is moved out to the callers, who has freedom to decide if additional analysis is needed in the first place
  // For OpenMP implementation, directives with clauses have sufficient information to guide variable handling, no other analysis is needed at all. 
  // ---------------------------------------------------------
  std::set< SgInitializedName *> liveIns, liveOuts;
  // Collect read-only variables of the outlining target
  std::set<SgInitializedName*> readOnlyVars;

  if (Outliner::temp_variable||Outliner::enable_classic)
  {
    SgStatement* firstStmt = (s->get_statements())[0];
    if (isSgForStatement(firstStmt)&& enable_liveness)
    {
      LivenessAnalysis * liv = SageInterface::call_liveness_analysis (SageInterface::getProject());
      SageInterface::getLiveVariables(liv, isSgForStatement(firstStmt), liveIns, liveOuts);
    }
    SageInterface::collectReadOnlyVariables(s,readOnlyVars);
    if (Outliner::enable_debug)
    {
      cout<<"Outliner::Transform::generateFunction() -----Found "<<readOnlyVars.size()<<" read only variables..:";
      for (std::set<SgInitializedName*>::const_iterator iter = readOnlyVars.begin();
          iter!=readOnlyVars.end(); iter++)
        cout<<" "<<(*iter)->get_name().getString()<<" ";
      cout<<endl;
      cout<<"Outliner::Transform::generateFunction() -----Found "<<liveOuts.size()<<" live out variables..:";
      for (std::set<SgInitializedName*>::const_iterator iter = liveOuts.begin();
          iter!=liveOuts.end(); iter++)
        cout<<" "<<(*iter)->get_name().getString()<<" ";
      cout<<endl;
    }
  }
#endif
  //step 2. Create function skeleton, 'func'.
  // -----------------------------------------
  SgName func_name (func_name_str);
  SgFunctionParameterList *parameterList = buildFunctionParameterList();

  SgFunctionDeclaration* func = createFuncSkeleton (func_name,SgTypeVoid::createType (),parameterList, scope);
  ROSE_ASSERT (func);

  // Liao, 4/15/2009 , enforce C-bindings  for C++ outlined code
  // enable C code to call this outlined function
  // Only apply to C++ , pure C has trouble in recognizing extern "C"
  // Another way is to attach the function with preprocessing info:
  // #if __cplusplus 
  // extern "C"
  // #endif
  // We don't choose it since the language linkage information is not explicit in AST
  // if (!SageInterface::is_Fortran_language())
  if ( SageInterface::is_Cxx_language() || is_mixed_C_and_Cxx_language() || is_mixed_Fortran_and_Cxx_language() || is_mixed_Fortran_and_C_and_Cxx_language() )
  {
    // Make function 'extern "C"'
    func->get_declarationModifier().get_storageModifier().setExtern();
    func->set_linkage ("C");
  }

  //step 3. Create the function body
  // -----------------------------------------
  // Generate the function body by deep-copying 's'.
  SgBasicBlock* func_body = func->get_definition()->get_body();
  ROSE_ASSERT (func_body != NULL);

  // This does a copy of the statements in "s" to the function body of the outlined function.
  ROSE_ASSERT(func_body->get_statements().empty() == true);
  SageInterface::moveStatementsBetweenBlocks (s, func_body);

  if (Outliner::useNewFile)
    ASTtools::setSourcePositionAtRootAndAllChildrenAsTransformation(func_body);

#if 0
  // We can't call this here because "s" is passed in as "cont".
  // DQ (2/24/2009): I think that at this point we should delete the subtree represented by "s"
  // But it might have made more sense to not do a deep copy on "s" in the first place.
  // Why is there a deep copy on "s"?
     SageInterface::deleteAST(s);
#endif

#if 0
  // Liao, 12/27/2007, for DO .. CONTINUE loop, bug 171
  // copy a labeled CONTINUE at the end when it is missing
  // SgFortranDo --> SgLabelSymbol --> SgLabelStatement (CONTINUE)
  // end_numeric_label  fortran_statement    numeric_label
  if (SageInterface::is_Fortran_language())
  {
    SgStatementPtrList stmtList = func_body->get_statements();
    ROSE_ASSERT(stmtList.size()>0);
    SgStatementPtrList::reverse_iterator stmtIter;
    stmtIter = stmtList.rbegin();
    SgFortranDo * doStmt = isSgFortranDo(*stmtIter);
    if (doStmt) {
      SgLabelSymbol* label1= doStmt->get_end_numeric_label();
      if (label1)
      {
        SgLabelSymbol* label2=isSgLabelSymbol(ASTtools::deepCopy(label1));
        ROSE_ASSERT(label2);
        SgLabelStatement * contStmt = isSgLabelStatement(ASTtools::deepCopy(label1->\
              get_fortran_statement()));
        ROSE_ASSERT(contStmt);

        func_body->insert_symbol(label2->get_name(),isSgSymbol(label2));
        doStmt->set_end_numeric_label(label2);
        contStmt->set_numeric_label(label2);
        func_body->append_statement(contStmt);
      }
    } // end doStmt
  }
#endif

  //step 4: variable handling, including: 
  // -----------------------------------------
  //   create parameters of the outlined functions
  //   add statements to unwrap the parameters if necessary
  //   add repacking statements if necessary
  //   replace variables to access to parameters, directly or indirectly
  //variableHandling(syms, pdSyms, readOnlyVars, liveOuts, struct_decl, func);
  unpack_stmts = variableHandling(syms, pdSyms, restoreVars, struct_decl, func);
  ROSE_ASSERT (func != NULL);
  
//     std::cout << func->get_type()->unparseToString() << std::endl;
//     std::cout << func->get_parameterList()->get_args().size() << std::endl;
     func->set_type(buildFunctionType(func->get_type()->get_return_type(), buildFunctionParameterTypeList(func->get_parameterList())));
//     std::cout << func->get_type()->unparseToString() << std::endl;

  // Retest this...
  ROSE_ASSERT(func->get_definition()->get_body()->get_parent() == func->get_definition());
  // printf ("After resetting the parent: func->get_definition() = %p func->get_definition()->get_body()->get_parent() = %p \n",func->get_definition(),func->get_definition()->get_body()->get_parent());
  //
  ROSE_ASSERT(scope->lookup_function_symbol(func->get_name()));
  return func;
}

//! Create a function named 'func_name_str' containing an OpenMP loop to be executed with Nanos
// (Inspired in generateFunction method)
// For an input 's' such as:
//    int i;
//    for (i = 0; i <= 9; i += 1) {
//        a[i] = (i * 2);
//    }
// The outlined function body 'func_body' will be:
//    int i;
//    int nanos_lower = ( (nanos_loop_info_t ) ((struct OUT__1__6388___data *)__out_argv) )->wsd.lower;
//    int nanos_upper = ( (nanos_loop_info_t ) ((struct OUT__1__6388___data *)__out_argv) )->wsd.upper;
//    int nanos_step = ( (nanos_loop_info_t ) ((struct OUT__1__6388___data *)__out_argv) )->wsd.step;
//    if( nanos_step > 0 )
//        for (i = nanos_lower; i <= nanos_upper; i += nanos_step)
//            a_0[i] = i * 2;
//    else
//        for (i = nanos_lower; i >= nanos_upper; i += nanos_step)
//            a_0[i] = i * 2;
SgFunctionDeclaration *
    Outliner::generateLoopFunction ( SgBasicBlock* s,
                                     const string& func_name_str,
                                     const ASTtools::VarSymSet_t& syms,
                                     const ASTtools::VarSymSet_t& pdSyms,
                                     const std::set< SgInitializedName *>& restoreVars,
                                     SgClassDeclaration* struct_decl, 
                                     SgScopeStatement* scope, 
                                     std::set<SgVariableDeclaration *>& unpacking_stmts )
{
    ROSE_ASSERT( s && scope );
    ROSE_ASSERT( isSgGlobal( scope ) );
    
    //step 1. Create function skeleton, 'func'.
    //---------------------------------------------------------------------------------------
    SgName func_name( func_name_str );
    SgFunctionParameterList* parameterList = buildFunctionParameterList( );
    SgFunctionDeclaration* func = createFuncSkeleton( func_name, buildVoidType( ), parameterList, scope );
    ROSE_ASSERT( func );
    
    if( SageInterface::is_Cxx_language( ) || is_mixed_C_and_Cxx_language( ) )
    { // Make function 'extern "C"'
        func->get_declarationModifier( ).get_storageModifier( ).setExtern( );
        func->set_linkage( "C" );
    }

    //step 2. Create the function body PART 1
    //---------------------------------------------------------------------------------------
    // Here we only copy the all the statements in 's' to the function body
    SgBasicBlock* func_body = func->get_definition( )->get_body( );
    ROSE_ASSERT( func_body != NULL );
    ROSE_ASSERT( func_body->get_statements( ).empty( ) == true );
    SageInterface::moveStatementsBetweenBlocks( s, func_body );

    if( Outliner::useNewFile )
        ASTtools::setSourcePositionAtRootAndAllChildrenAsTransformation( func_body );
    
    //step 3: variable handling, including: 
    //---------------------------------------------------------------------------------------
    unpacking_stmts = variableHandling( syms, pdSyms, restoreVars, struct_decl, func );
    ROSE_ASSERT( func != NULL );
    func->set_type( buildFunctionType( func->get_type()->get_return_type( ), 
                    buildFunctionParameterTypeList( func->get_parameterList( ) ) ) );
    // Retest this...
    ROSE_ASSERT( func_body->get_parent( ) == func->get_definition( ) );
    ROSE_ASSERT( scope->lookup_function_symbol( func->get_name( ) ) );
    
    //step 4. Create the function body PART 2
    //---------------------------------------------------------------------------------------
    // Here we introduce in the function code the statements for Nanos calls
    // We split the construction of the function body because 
    // we need variable handling to be done before building the nanos calls
    // and we need the original code to be placed in the function code 
    // before the handling is made because it can cause replacement
    SgClassSymbol * loop_info_sym = SageInterface::lookupClassSymbolInParentScopes( "nanos_loop_info_t", scope );
    SgVariableDeclaration * member_lower, * member_upper, * member_step;
    if( loop_info_sym == NULL )
    {
        // Generate the struct type definition to avoid errors durint AstConsistencyTests
        // Don't add this in the outlined code because the struct is defined in nanos-int.h
        SgClassDeclaration * loop_info_decl = buildStructDeclaration( "nanos_loop_info_t", SageInterface::getGlobalScope( scope ) );
        SgClassDefinition * loop_info_def = loop_info_decl->get_definition( );
        ROSE_ASSERT( loop_info_def != NULL );
        member_lower = buildVariableDeclaration( "lower", buildIntType( ), NULL, loop_info_def );
        member_upper = buildVariableDeclaration( "upper", buildIntType( ), NULL, loop_info_def );
        member_step = buildVariableDeclaration( "step", buildIntType( ), NULL, loop_info_def );
        loop_info_def->append_member( member_lower );
        loop_info_def->append_member( member_upper );
        loop_info_def->append_member( member_step );
        loop_info_decl->set_definition( loop_info_def );
        
        loop_info_sym = isSgClassSymbol( loop_info_decl->search_for_symbol_from_symbol_table( ) );
        ROSE_ASSERT( loop_info_sym != NULL ); 
        loop_info_sym->set_declaration( loop_info_decl );
        ROSE_ASSERT( loop_info_sym->get_declaration( )->get_definition( ) != NULL );
    }
    else
    {
        SgClassDeclaration * loop_info_decl = loop_info_sym->get_declaration( );
        SgClassDefinition * loop_info_def = loop_info_decl->get_definition( );
        SgDeclarationStatementPtrList loop_info_members = loop_info_def->get_members( );
        
        member_lower = isSgVariableDeclaration( loop_info_members[0] );
        member_upper = isSgVariableDeclaration( loop_info_members[1] );
        member_step = isSgVariableDeclaration( loop_info_members[2] );
    }
    
    // Get the WSD member from the __out_argv parameter
    // (nanos_loop_info_t ) ( ( (struct OUT__1__6388___data *)__out_argv )->wsd )
    SgInitializedNamePtrList& argsList = func->get_parameterList( )->get_args( );
    ROSE_ASSERT( argsList.size() == 1 );      // The only argument must be __out_argv
    SgDeclarationStatementPtrList st_members = struct_decl->get_definition( )->get_members( );
    SgVariableDeclaration * wsd_member = isSgVariableDeclaration( *( st_members.begin( ) ) );
    SgInitializedName * wsd_name = wsd_member->get_decl_item( "wsd" );
    SgInitializedName * n = *argsList.begin( );
    SgExpression * param_expr = buildVarRefExp( n, func_body );
    SgExpression * wsd_expr = buildVarRefExp( wsd_name, func_body );
    // We don't cast to 'loop_info_sym->get_type( )' 
    // because 'nanos_loop_info_t' is a typedef in 'nanos-int.h', so we don't need to name 'struct'
    SgExpression * wsd = buildCastExp( buildArrowExp( buildCastExp( param_expr, buildPointerType( struct_decl->get_type( ) ) ),
                                                      wsd_expr ),
                                       buildOpaqueType( "nanos_loop_info_t", func_body ) );

    // Create variables containing the lower and upper bounds and the step of the nanos loop using WSD and replace the loop boundaries
    //     int nanos_lower = ((nanos_loop_info_t )(((struct OUT__2__6660___data *)__out_argv) -> wsd)).lower;
    //     int nanos_upper = ((nanos_loop_info_t )(((struct OUT__2__6660___data *)__out_argv) -> wsd)).upper;
    //     int nanos_step = ((nanos_loop_info_t )(((struct OUT__2__6660___data *)__out_argv) -> wsd)).step
    SgExpression * wsd_lower = buildDotExp( wsd, buildVarRefExp( member_lower->get_decl_item( "lower" ), func_body ) );
    SgExpression * wsd_upper = buildDotExp( wsd, buildVarRefExp( member_upper->get_decl_item( "upper" ), func_body ) );
    SgExpression * wsd_step = buildDotExp( wsd, buildVarRefExp( member_step->get_decl_item( "step" ), func_body ) );   
    SgInitializer * lower_init = buildAssignInitializer( wsd_lower, buildIntType( ) );
    SgVariableDeclaration * lower = buildVariableDeclaration( "nanos_lower", buildIntType( ), lower_init, func_body );
    SgInitializer * upper_init = buildAssignInitializer( wsd_upper, buildIntType( ) );
    SgVariableDeclaration * upper = buildVariableDeclaration( "nanos_upper", buildIntType( ), upper_init, func_body );
    SgInitializer * step_init = buildAssignInitializer( wsd_step, buildIntType( ) );
    SgVariableDeclaration * step = buildVariableDeclaration( "nanos_step", buildIntType( ), step_init, func_body );
    Rose_STL_Container<SgNode *> loop_list = NodeQuery::querySubTree( func_body, V_SgForStatement );
    ROSE_ASSERT( !loop_list.empty( ) ); 
    SgForStatement * original_loop = isSgForStatement( *( loop_list.begin( ) ) );
    insertStatementBefore( original_loop, lower );
    insertStatementBefore( original_loop, upper );
    insertStatementBefore( original_loop, step );
    
    // Create the two possible loops depending on the sign of the step
    // Input:
    //     for( int i = LB; i < UB; i += stride ) { ... }
    // Output:
    //     if( stride > 0 )
    //        for( int i = nanos_LB; i <= nanos_UB; i += stride ) { ... }
    //     else
    //        for( int i = nanos_LB; i >= nanos_UB; i += stride ) { ... }
    SageInterface::setLoopLowerBound( original_loop, buildVarRefExp( lower ) );
    SageInterface::setLoopUpperBound( original_loop, buildVarRefExp( upper ) );
    SageInterface::setLoopStride( original_loop, buildVarRefExp( step ) ); 
    SgExpression * loop_test = isSgExprStatement( original_loop->get_test( ) )->get_expression( );
    if( isSgEqualityOp( loop_test ) || isSgGreaterOrEqualOp( loop_test ) || isSgGreaterThanOp( loop_test )
        || isSgLessOrEqualOp( loop_test ) || isSgLessThanOp( loop_test ) || isSgNotEqualOp( loop_test ) )
    {
        SgBinaryOp * loop_test_op = isSgBinaryOp( loop_test );
        
        SgForStatement * true_loop = isSgForStatement( SageInterface::copyStatement( original_loop ) );
        SgStatement * true_loop_test = buildExprStatement(
                buildLessOrEqualOp( SageInterface::copyExpression( loop_test_op->get_lhs_operand( ) ),
                                    SageInterface::copyExpression( loop_test_op->get_rhs_operand( ) ) ) );
        true_loop->set_test( true_loop_test );
        true_loop_test->set_parent( true_loop );
        
        SgForStatement * false_loop = isSgForStatement( SageInterface::copyStatement( original_loop ) );
        SgStatement * false_loop_test = buildExprStatement(
                buildGreaterOrEqualOp( SageInterface::copyExpression( loop_test_op->get_lhs_operand( ) ),
                                       SageInterface::copyExpression( loop_test_op->get_rhs_operand( ) ) ) );
        false_loop->set_test( false_loop_test );
        false_loop_test->set_parent( false_loop );
        
        SgExpression * cond = buildGreaterThanOp( buildVarRefExp( step ), buildIntVal( 0 ) );
        SgStatement * if_stmt = buildIfStmt( cond, true_loop, false_loop );
        
        SageInterface::replaceStatement( original_loop, if_stmt );
    }
    else
    {
        std::cerr << "error. generateLoopFunction(): non comparison operation '" << loop_test->unparseToString( ) 
                  << "' case is not yet handled !" << std::endl;
        ROSE_ASSERT( false );
    }
    
    return func;
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
SgFunctionDeclaration * 
    Outliner::generateSectionsFunction( SgBasicBlock* s,
                                        const std::string& func_name_str,
                                        const ASTtools::VarSymSet_t& syms,
                                        const ASTtools::VarSymSet_t& pdSyms,
                                        const std::set<SgInitializedName *>& restoreVars,       // last private variables that must be restored
                                        SgClassDeclaration* struct_decl,
                                        SgScopeStatement* scope, 
                                        std::set<SgVariableDeclaration *>& unpacking_stmts )
{
    ROSE_ASSERT( s && scope );
    ROSE_ASSERT( isSgGlobal( scope ) );
    
    //step 1. Create function skeleton, 'func'.
    //---------------------------------------------------------------------------------------
    SgFunctionParameterList* parameterList = buildFunctionParameterList( );
    SgFunctionDeclaration* func = createFuncSkeleton( func_name_str, buildVoidType( ), parameterList, scope );
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
    //     err = nanos_worksharing_next_item( data->wsd, ( void ** ) &nanos_item_loop );
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
    SgWhileStmt * nanos_while_body = buildWhileStmt( while_cond, while_body );
    appendStatement( nanos_while_body, func_body );
    
    if( Outliner::useNewFile )
        ASTtools::setSourcePositionAtRootAndAllChildrenAsTransformation( func_body );
    
    //step 3: variable handling: create parameters, packing and unpacking statements and replace variables
    //---------------------------------------------------------------------------------------
    unpacking_stmts = variableHandling( syms, pdSyms, restoreVars, struct_decl, func );
    
    //step 4: parameters checking
    //---------------------------------------------------------------------------------------
    // Member 'wsd' is only needed when there is a reduction performed sections construct
    // But we add it always to match the header of the Nanos sections method
    SgFunctionParameterList * params = func->get_parameterList( );
    SgInitializedName * wsd_member = buildInitializedName( "wsd", buildPointerType( buildOpaqueType( "nanos_ws_desc_t", func_body ) ) );
    SageInterface::appendArg( params, wsd_member );
    func->set_type( buildFunctionType( func->get_type()->get_return_type( ), 
                    buildFunctionParameterTypeList( params ) ) );
    
    // Retest this...
    ROSE_ASSERT( func_body->get_parent( ) == func->get_definition( ) );
    ROSE_ASSERT( scope->lookup_function_symbol( func->get_name( ) ) );
    
    return func;
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
SgFunctionDeclaration *
        Outliner::generateReductionFunction( SgBasicBlock* s,
                                             const std::string& func_name_str,
                                             const ASTtools::VarSymSet_t& reduction_syms,
                                             SgClassDeclaration* struct_decl,
                                             SgScopeStatement* scope, 
                                             std::set<SgVariableDeclaration *>& unpacking_stmts )
{
    ROSE_ASSERT( s && scope );
    ROSE_ASSERT( isSgGlobal( scope ) );
    
    //step 1. Create function skeleton, 'func'
    //---------------------------------------------------------------------------------------
    SgFunctionParameterList* parameterList = buildFunctionParameterList( );
    SgFunctionDeclaration* func = createFuncSkeleton( func_name_str, buildVoidType( ), parameterList, scope );
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
SgFunctionDeclaration *
        Outliner::generateReductionWrapperFunction( SgFunctionDeclaration * func,
                                                    SgOmpClauseBodyStatement * target,
                                                    const std::string& func_name_str,
                                                    const ASTtools::VarSymSet_t& syms,
                                                    const ASTtools::VarSymSet_t& pdSyms,
                                                    const ASTtools::VarSymSet_t& reduction_syms,
                                                    SgClassDeclaration* struct_decl,
                                                    SgScopeStatement* scope, 
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

// eof
