#include "expressionHandler.h"
#include "utilities/utilities.h"
#include "pluggableReverser/eventProcessor.h"
#include <boost/foreach.hpp>

#define foreach BOOST_FOREACH
#define reverse_foreach BOOST_REVERSE_FOREACH


using namespace std;
using namespace SageBuilder;

/******************************************************************************
 **** Definition of member functions of IdentityExpressionHandler  ***********/

ExpressionReversal IdentityExpressionHandler::generateReverseAST(SgExpression* exp, const EvaluationResult& evaluationResult)
{
	ROSE_ASSERT(evaluationResult.getExpressionHandler() == this && evaluationResult.getChildResults().size() == 0);
	bool reverseIsNull = evaluationResult.getAttribute<bool>();

	SgExpression* forwardExpression = SageInterface::copyExpression(exp);
	SgExpression* reverseExpression;
	if (reverseIsNull)
	{
		reverseExpression = NULL;
	}
	else
	{
		reverseExpression = SageInterface::copyExpression(exp);
	}

	return ExpressionReversal(forwardExpression, reverseExpression);
}

EvaluationResult IdentityExpressionHandler::evaluate(SgExpression* exp, const VariableVersionTable& var_table, bool reverseValueUsed)
{
	// If an expression does not modify any value and its value is used, the reverse is the same as itself
	if (!BackstrokeUtility::containsModifyingExpression(exp))
	{
		EvaluationResult result(this, exp, var_table);
		result.setAttribute(!reverseValueUsed);
		return result;
	}

	return EvaluationResult();
}

/******************************************************************************
 **** Definition of member functions of StoreAndRestoreExpressionHandler ****/

ExpressionReversal StoreAndRestoreExpressionHandler::generateReverseAST(SgExpression* exp, const EvaluationResult& evaluationResult)
{
	SgExpression* var_to_save = evaluationResult.getAttribute<SgExpression*>();
	ROSE_ASSERT(var_to_save);

	SgExpression* fwd_exp = buildBinaryExpression<SgCommaOpExp>(
			pushVal(SageInterface::copyExpression(var_to_save)),
			SageInterface::copyExpression(exp));
	SgExpression* rvs_exp = buildBinaryExpression<SgAssignOp>(
			SageInterface::copyExpression(var_to_save),
			popVal(var_to_save->get_type()));

	return ExpressionReversal(fwd_exp, rvs_exp);
}

EvaluationResult StoreAndRestoreExpressionHandler::evaluate(SgExpression* exp, const VariableVersionTable& var_table, bool is_value_used)
{
	SgExpression* var_to_save = NULL;

	if (isSgPlusPlusOp(exp) || isSgMinusMinusOp(exp))
		var_to_save = isSgUnaryOp(exp)->get_operand();
	else if (SageInterface::isAssignmentStatement(exp))
		var_to_save = isSgBinaryOp(exp)->get_lhs_operand();

	if (var_to_save == NULL)
		return EvaluationResult();

	if (VariableRenaming::getVarName(var_to_save) != VariableRenaming::emptyName)
	{
		SgType* varType = VariableRenaming::getVarName(var_to_save).back()->get_type();
		if (SageInterface::isPointerType(varType))
		{
			fprintf(stderr, "ERROR: Correctly saving pointer types not yet implemented (it's not hard)\n");
			fprintf(stderr, "The pointer is saved an restored, rather than the value it points to!\n");
			fprintf(stderr, "Variable %s\n", var_to_save->unparseToString().c_str());
			ROSE_ASSERT(false);
		}

		// Update the variable version table.
		VariableVersionTable new_var_table = var_table;
		new_var_table.reverseVersion(var_to_save);

		// Update the cost.
		SimpleCostModel cost;
		cost.increaseStoreCount();

		EvaluationResult result(this, exp, new_var_table, cost);
		result.setAttribute(var_to_save);
		return result;
	}

	return EvaluationResult();
}
