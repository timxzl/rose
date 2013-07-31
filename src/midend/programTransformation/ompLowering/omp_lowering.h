#include "astQuery.h"
//#include "sage3basic.h"

#include "nanos_ompss.h"

/*!
 * Translation (directive lowering) support for OpenMP 3.0 C/C++
 *
 *  Like other OpenMP related work within ROSE, 
 *  all things go to the OmpSupport namespace to avoid conflicts
 * Liao, 8/10/2009
 */
#ifndef OMP_LOWERING_H
#define OMP_LOWERING_H 

namespace OmpSupport
{

  // OpenMP version info.
  extern bool enable_accelerator;  

  //! makeDataSharingExplicit() can call some of existing functions for some work in OmpSupport namespace by Hongyi 07/16/2012
  //! TODO: add a function within the OmpSupport namespace, the function should transform the AST, so all variables' data-sharing attributes are explicitied represented in the AST. ROSE has dedicated AST nodes for OpenMP directives and the associated clauses, such as private, shared, reduction.


  int patchUpSharedVariables(SgFile* );
  // TODO:  patchUpDefaultVariables(SgFile* );

  int makeDataSharingExplicit( SgFile* );   


  // last edited by Hongyi on 07/24/2012. 

  //! The type of target runtime libraries (not yet in use)
  // We support Omni, GCC OpenMP and NANOS++ runtime libraries
  enum omp_rtl_enum 
  {
    e_gomp,
    e_omni,
    e_nanos,
    e_last_rtl
  };
  extern unsigned int nCounter; // translation generated variable counter, used to avoid name collision

  extern omp_rtl_enum rtl_type; 
  typedef std::map<const SgVariableSymbol *, SgVariableSymbol *> VariableSymbolMap_t;

  void commandLineProcessing(std::vector<std::string> &argvList);

  //! The top level interface to translate OpenMP directives
  void lower_omp(SgSourceFile*);


  //! Insert #include "xxx.h", the interface of a runtime library to the compiler
  void insertRTLHeaders(SgSourceFile*);

  //! Insert runtime init and terminate routines to main() entry
  void insertRTLinitAndCleanCode(SgSourceFile* ); 

  //! A driver to traverse AST trees and invoke individual translators for OpenMP constructs, (not in use)
  //! Postorder is preferred. 
  class translationDriver: public AstSimpleProcessing
  { 
    protected:
      void visit(SgNode*);
  }; //translationDriver

  //! Translate omp parallel
  void transOmpParallel(SgNode* node);

  //! Translate omp parallel under "omp target"
  void transOmpTargetParallel(SgNode* node);

  //! Translate omp sections 
  void transOmpSections(SgNode* node);

  //! Translate omp task
  void transOmpTask(SgNode* node);

  //! Translate omp for or omp do loops
  void transOmpLoop(SgNode* node);

  //! Translate omp for or omp do loops affected by the "omp target" directive, Liao 1/28/2013
  void transOmpTargetLoop(SgNode* node);

  //! Translate Fortran omp do
  //void transOmpDo(SgNode* node);

  //! Translate "omp target"
  void transOmpTarget(SgNode* node);

  //! Translate "omp target data"
  void transOmpTargetData(SgNode* node);


  //! Translate omp barrier
  void transOmpBarrier(SgNode* node);

  //! Translate omp flush 
  void transOmpFlush(SgNode* node);

  //! Translate omp taskwait
  void transOmpTaskwait(SgNode* node);

  //! Translate omp threadprivate
  void transOmpThreadprivate(SgNode* node);

  //! Translate the ordered directive (not the ordered clause)
  void transOmpOrdered(SgNode* node);
  //! Translate omp atomic
  void transOmpAtomic(SgNode* node);
  //! Translate omp critical 
  void transOmpCritical(SgNode* node);
  //! Translate omp master 
  void transOmpMaster(SgNode* node);
  //! Translate omp single 
  void transOmpSingle(SgNode* node);

  //! A helper function to generate implicit or explicit task for either omp parallel or omp task
  // It calls the ROSE AST outliner internally. 
  SgFunctionDeclaration* generateOutlinedTask(SgNode* node, std::string& wrapper_name, 
          std::set<SgVariableSymbol*>& syms, SgClassDeclaration*& struct_decl);

  //! Translate OpenMP variables associated with an OpenMP pragma, such as private, firstprivate, lastprivate, reduction, etc. bb1 is the translation generated code block in which the variable handling statements will be inserted. Original loop upper bound is needed for implementing lastprivate (check if it is the last iteration). withinAcceleratorModel means if we only translate private() variables, used to support accelerator model
  ROSE_DLL_API void transOmpVariables(SgStatement * ompStmt, SgBasicBlock* bb1, SgExpression* orig_loop_upper = NULL, bool withinAcceleratorModel= false);

  //! Collect all variables from OpenMP clauses associated with an omp statement: private, reduction, etc 
  ROSE_DLL_API SgInitializedNamePtrList collectAllClauseVariables (SgOmpClauseBodyStatement * clause_stmt);

  //! Collect variables from a particular type of OpenMP clauses associated with an omp statement: private, reduction, etc 
  ROSE_DLL_API SgInitializedNamePtrList collectClauseVariables (SgOmpClauseBodyStatement * clause_stmt, const VariantT& vt);

  //! Collect variables from given types of OpenMP clauses associated with an omp statement: private, reduction, etc 
  ROSE_DLL_API SgInitializedNamePtrList collectClauseVariables (SgOmpClauseBodyStatement * clause_stmt, const VariantVector& vvt);

  //! Check if a variable is in a variable list of a given clause type
  ROSE_DLL_API bool isInClauseVariableList(SgInitializedName* var, SgOmpClauseBodyStatement * clause_stmt, const VariantT& vt);

  //! Check if a variable is in variable lists of given clause types
  ROSE_DLL_API bool isInClauseVariableList(SgInitializedName* var, SgOmpClauseBodyStatement * clause_stmt, const VariantVector& vvt);

  //! Replace references to oldVar within root with references to newVar, return the number of references replaced.
  ROSE_DLL_API int replaceVariableReferences(SgNode* root, SgVariableSymbol* oldVar, SgVariableSymbol* newVar);

  //! Replace variable references within root based on a map from old symbols to new symbols
  ROSE_DLL_API int replaceVariableReferences(SgNode* root,  VariableSymbolMap_t varRemap);
  // I decided to reuse the existing Outliner work instead of coding a new one
  //SgFunctionDeclaration* generateOutlinedFunction(SgNode* node);

  //! Replace all variable references in a set by pointers to the variable
  ROSE_DLL_API int replaceVariablesWithPointerDereference(SgNode* root, std::set<SgVariableSymbol*>& vars);
  
  //! Add a variable into a non-reduction clause of an OpenMP statement, create the clause transparently if it does not exist
  ROSE_DLL_API void addClauseVariable(SgInitializedName* var, SgOmpClauseBodyStatement * clause_stmt, const VariantT& vt);

  //! Build a non-reduction variable clause for a given OpenMP directive. It directly returns the clause if the clause already exists
  ROSE_DLL_API SgOmpVariablesClause* buildOmpVariableClause(SgOmpClauseBodyStatement * clause_stmt, const VariantT& vt);

  //! Check if an OpenMP statement has a clause of type vt
  ROSE_DLL_API bool hasClause(SgOmpClauseBodyStatement* clause_stmt, const VariantT & vt);

  //! Get OpenMP clauses from an eligible OpenMP statement
  ROSE_DLL_API Rose_STL_Container<SgOmpClause*>  getClause(SgOmpClauseBodyStatement* clause_stmt, const VariantT & vt);

  //! Check if an omp for/do loop use static schedule or not, including: default schedule, or schedule(static[,chunk_size]) 
  ROSE_DLL_API bool useStaticSchedule(SgOmpClauseBodyStatement* omp_loop);

  //! Return a reduction variable's reduction operation type
  ROSE_DLL_API SgOmpClause::omp_reduction_operator_enum getReductionOperationType(SgInitializedName* init_name, SgOmpClauseBodyStatement* clause_stmt);

  //! Create an initial value according to reduction operator type
  ROSE_DLL_API SgExpression* createInitialValueExp(SgOmpClause::omp_reduction_operator_enum r_operator);

  //! Generate GOMP loop schedule start function's name
  ROSE_DLL_API std::string generateGOMPLoopStartFuncName (bool isOrdered, SgOmpClause::omp_schedule_kind_enum s_kind);

  //! Generate GOMP loop schedule next function's name
  ROSE_DLL_API std::string generateGOMPLoopNextFuncName (bool isOrdered, SgOmpClause::omp_schedule_kind_enum s_kind);

  //! Convert a schedule kind enum value to a small case string
  ROSE_DLL_API std::string toString(SgOmpClause::omp_schedule_kind_enum s_kind);

  //! Patch up private variables for omp for. The reason is that loop indices should be private by default and this function will make this explicit
  ROSE_DLL_API int patchUpPrivateVariables(SgFile*);

  //! Patch up firstprivate variables for omp task. The reason is that the specification 3.0 defines rules for implicitly determined data-sharing attributes and this function will make the firstprivate variable of omp task explicit.
  ROSE_DLL_API int patchUpFirstprivateVariables(SgFile*);

  //! Collect threadprivate variables within the current project, return a set to avoid duplicated elements. No input parameters are needed since it finds match from memory pools
  std::set<SgInitializedName*> collectThreadprivateVariables();

  SgBasicBlock* generateArrayAssignmentStatements( SgExpression* left_operand, SgExpression* right_operand );
  
  //! Generate a symbol set from an initialized name list, 
  //filter out struct/class typed names
  void getSymbolfromInitName (const SgInitializedNamePtrList input, std::set<const SgVariableSymbol*>& output);
  
  //! Special handling when trying to build and insert a variable declaration into a BB within Fortran OpenMP code
  SgVariableDeclaration * buildAndInsertDeclarationForOmp(const std::string &name, SgType *type, SgInitializer *varInit, SgBasicBlock *orig_scope);
  //! Find an enclosing parallel region or function definition's body
  SgBasicBlock* getEnclosingRegionOrFuncDefinition (SgNode *);
  
  
  namespace NanosLowering {
    
    /*!\brief Generate a struct declaration to wrap all variables to be passed to the outlined function
     * There are two ways to wrap a variable: Using its value vs. Using its address (pointer type)
     *
     * This function will also insert the declaration inside the global scope point right before the outlining target
     * If the scope of the outlined function is different, a declaration will also be inserted there. 
     */
    ROSE_DLL_API
    SgClassDeclaration* generateParameterStructureDeclarationNanos( SgBasicBlock* s, // the outlining target
                                                                    const std::string& func_name_str, // the name for the outlined function, we generate the name of struct based on this.
                                                                    const std::set<const SgVariableSymbol*>& syms, // variables to be passed as parameters
                                                                    const std::set<const SgVariableSymbol*>& pdSyms, // variables must use pointer types (pointer dereferencing: pdf). The rest variables use pass-by-value
                                                                    SgScopeStatement* func_scope, // the scope of the outlined function, could be in another file
                                                                    const std::set<const SgVariableSymbol*>& nanos_red_syms ); //set of reduction symbols ( only usefull if Nanos RTL )   
      
    //! Generate packing (wrapping) statements for the variables to be passed 
    ROSE_DLL_API
    std::string generatePackingStatementsNanos( SgStatement * target, 
                                                const std::set<const SgVariableSymbol*>& syms,
                                                const std::set<const SgVariableSymbol*>& pdsyms,
                                                SgClassDeclaration * struct_decl,
                                                const std::set<const SgVariableSymbol*>& redution_syms );
    
    //! Outlines a function containing an OpenMP worksharing (loop or sections) and includes the Nanos calls to execute it in parallel
    SgFunctionDeclaration* generateOutlinedWorksharing( SgOmpClauseBodyStatement * source, std::string & wrapper_name, 
                                                        std::set<const SgVariableSymbol*>& syms, std::set<const SgVariableSymbol*>& pdSyms3, 
                                                        SgClassDeclaration * & struct_decl, worksharing_type_enum ws_type );
    
    //! Outlines a function containing an OpenMP and includes the Nanos calls to execute it in parallel
    void generate_nanos_reduction( SgFunctionDeclaration * func,
                                   SgOmpClauseBodyStatement * target, SgClassDeclaration*& struct_decl, std::string func_name,
                                   std::set<const SgVariableSymbol*> syms, std::set<const SgVariableSymbol*> pdSyms, 
                                   std::set<SgVariableDeclaration*> unpacking_stmts );
    
    //! Create an empty object with type the struct to be passed to an OpenMP outlined function in Nanos
    //! Returns an expression containing the new object
    SgExpression* build_nanos_empty_struct( SgStatement* omp_stmt, SgScopeStatement* stmt_sc, 
                                            SgType* struct_type, std::string base_name );

    //! Create the function retrieving the empty struct to be passed to an OpenMP outlined function in Nanos
    //! Returns an expression containing a call to the function
    SgExpression* build_nanos_get_empty_struct( SgStatement* ancestor, SgScopeStatement* expr_sc, 
                                                SgType* struct_type, std::string base_name );
    
    //! Create the function that initializes an empty structure with the arguments to the outlined OpenMP parallel or task in Nanos
    SgExpression* build_nanos_init_arguments_struct_function( SgStatement* ancestor, std::string& wrapper_name, 
                                                              SgClassDeclaration* struct_decl );
    
    //! Create the function that retrieves the alignement of an struct
    SgExpression* build_nanos_get_alignof( SgStatement* ancestor, std::string& wrapper_name, 
                                           SgClassDeclaration* struct_decl );
    
    void get_dependency_clauses( SgOmpTaskStatement * task, SgExprListExp * & dependences_direction, 
                                 SgExprListExp * & dependences_data, int & n_deps );
    
    SgExpression * build_nanos_dependencies_array( SgExprListExp * dependences, std::string & array_name, SgArrayType * array_type,
                                                   SgOmpTaskStatement * task, SgScopeStatement * scope, bool build_data );
    
    //! Retrieves dependencies information by parsing the depend clauses of a task construct
    void build_nanos_dependencies_dimension_array( std::string & all_dims_name, std::string & n_dims_name, std::string & offsets_name,
                                                   SgExprListExp * dependences_data, 
                                                   SgOmpTaskStatement * task, SgScopeStatement * scope,
                                                   SgExpression * & all_dims_ref, SgExpression * & n_dims_ref, SgExpression * & offsets_ref );
    
    //! Returns the base symbol of a expression that can only contain, VarRefs or any form of arrays 
    //! (shape expression, array section, array element access )
    SgVarRefExp * get_dependecy_base_symbol_exp( SgExpression * dep_expr );
  }
  
} // end namespace OmpSupport  

#endif //OMP_LOWERING_H
