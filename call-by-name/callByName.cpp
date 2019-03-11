#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <iostream>

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Parse/ParseAST.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Rewrite/Frontend/Rewriters.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace std;

int thunk_id = 0;
bool headerFilesDone = false;
string currentRetType;
map<string, bool> processedMap;
map<string, string> thunkMap;
map<string, string> typeMap;

bool insertProcessedEntry(string key)
{
	processedMap.insert(pair<string, bool>(key,true));
}

bool findProcessedEntry(string key)
{
	if (processedMap.find(key) != processedMap.end())
		return true;

	return false;
}

class MyASTVisitor : public RecursiveASTVisitor<MyASTVisitor> {
public:
	MyASTVisitor(Rewriter &R) : TheRewriter(R) {}

void insertHeaderFiles()
{
	if (headerFilesDone)
		return;
	string thunk = "#include <assert.h>\n";
	SourceManager &sm = TheRewriter.getSourceMgr();
	FileID  fid = sm.getMainFileID();
	SourceLocation loc = sm.getLocForStartOfFile(fid);
	TheRewriter.InsertText(loc, thunk, true, true);
	headerFilesDone = true;
}

bool isConstant(string s)
{
	if(s.empty() || ((!isdigit(s[0])) && (s[0] != '-') && (s[0] != '+'))) return false;
	char * p;
	strtol(s.c_str(), &p, 10);
	return (*p == 0);
}

bool isPointer(string s)
{
	if (s.find(" *") != string::npos)
		return true;

	return false;
}

bool isArrayDecl(string s)
{
	if (s.find("[") != string::npos)
		return true;

	return false;
}

bool isAddress(string s)
{
	if (s.find("&") != string::npos)
		return true;

	return false;
}

bool isPrimitiveType(string s)
{
	if (isPointer(s) || isArrayDecl(s) || isAddress(s))
		return false;

	return true;
}

void collectExpressionVariable(Expr *expr, vector<string> &operands)
{
	SourceRange sr = expr->getSourceRange();
	SourceManager &sm = TheRewriter.getSourceMgr();
	const LangOptions &opt = TheRewriter.getLangOpts();
	CharSourceRange range = CharSourceRange::getTokenRange(sr.getBegin(), sr.getEnd());
	const string text = Lexer::getSourceText(range, sm, opt);
	if (isConstant(text))
		return;
	/* push each element no more than once */
	if (find(operands.begin(), operands.end(), text) == operands.end())
		operands.push_back(text);
}

void collectExpressionOperands(BinaryOperator *bop, vector<string> &operands)
{
	Expr *lhs = bop->getLHS();
	Expr *rhs = bop->getRHS();
	/* process lhs and rhsm recursively if needed */
	if (!isa<BinaryOperator>(lhs))
		collectExpressionVariable(lhs, operands);
	else
		collectExpressionOperands(cast<BinaryOperator>(lhs), operands);

	if (!isa<BinaryOperator>(rhs))
		collectExpressionVariable(rhs, operands);
	else
		collectExpressionOperands(cast<BinaryOperator>(rhs), operands);
}

void rewriteExpressionForThunk(string &str, vector<string> operands)
{
	for (int i = 0; i < operands.size(); i++)
	{
		int prev_pos = 0;
		while(true) {
			/* start after the previous index */
			int pos = str.find(operands[i], prev_pos);
			prev_pos = pos + 18;
			if (pos == string::npos)
				break;

			string newString = operands[i];
			if (!isConstant(operands[i]))
				newString = "(*(ptr_thunk_" + to_string(thunk_id) + "->" + operands[i] + "))";
			str.replace(pos, operands[i].length(), newString);
		}
	}
}

void createThunkStruct(vector<string> operands, string &thunkStruct)
{
	thunkStruct += "struct struct_thunk_" + to_string(thunk_id) + "\n";
	thunkStruct += "{\n";
	for (int i = 0; i < operands.size(); i++) {
		//thunkStruct += "\tint\t*" + operands[i] + ";\n";
		string type = typeMap.find(operands[i])->second;
		thunkStruct += "\t" + type + "\t*" + operands[i] + ";\n";
	}
	thunkStruct += "} *ptr_thunk_" + to_string(thunk_id) +";\n\n";
}

int findArgumentIndex(FunctionDecl *f, string arg)
{
	int index = 0;
	MutableArrayRef<ParmVarDecl *> params = f->parameters();
	MutableArrayRef<ParmVarDecl *>::iterator ptr;
	MutableArrayRef<ParmVarDecl *>::iterator end;
	for (ptr = params.begin(), end = params.end(); ptr != end; ptr = ptr + 1) {
		ParmVarDecl *par = *ptr;
		const NamedDecl *namedDecl = dyn_cast<NamedDecl>(par);
		string name = namedDecl->getNameAsString();
		/* special handling for pointers */
		//cout << "comparing " << name << " with " << arg << "\n";
		if (arg[0] == '*')
			arg = arg.substr(1);
		if (name.compare(arg) == 0)
			return index;
		index++;
	}
	/* we should not reach here */
	/* the argument should be either local or a parameter */
	assert(false);
}

string getTypeFromOperands(vector<string> operands)
{
	if (operands.size() == 1){
		string type = typeMap.find(operands[0])->second;
		return type;
	}
	for (int i = 0; i < operands.size(); i++) {
		string type = typeMap.find(operands[i])->second;
		if (type.find("float") != string::npos)
			return "float";
	}
	return "int";
}

void createThunkForExpression(FunctionDecl *f, int index, Expr *expr, string str, string &thunkBody,
				string &thunkStruct, string &thunkCaller, string &callString)
{
	string funcName = f->getNameInfo().getName().getAsString();
	vector <string> operands;
	collectExpressionOperands(cast<BinaryOperator>(expr), operands);
	/* hack to update variable type */
	for(int i = 0; i < operands.size(); i++) {
		isLocalVariable(f, operands[i]);
	}
	createThunkStruct(operands, thunkStruct);
	string returnType = getTypeFromOperands(operands);
	currentRetType = returnType;
	//thunkBody += "int gthunk_" + funcName + "_" + to_string(thunk_id);
	thunkBody += returnType + "* gthunk_" + funcName + "_" + to_string(thunk_id);
	thunkBody += "()\n{\n\t";
	rewriteExpressionForThunk(str, operands);
	//thunkBody += "int tmp;\n\ttmp = " + str + ";\n\treturn tmp;\n}\n\n";
	thunkBody += returnType + " tmp, *addr;\n\ttmp = " + str + ";\n\t";
	thunkBody += "addr = malloc(sizeof(" + returnType +"));\n\t";
	thunkBody += "assert(addr != NULL);\n\t";
	thunkBody += "*addr = " + str + ";\n\t";
	thunkBody += "return addr;\n}\n\n";
	//cout << thunkBody << "\n"; 
	/* increment the global counter */
	string thunkName = "ptr_thunk_"+ to_string(thunk_id);
	thunkCaller = thunkName + " = malloc(sizeof(struct struct_thunk_" + to_string(thunk_id) + "));\n";
	for (int i = 0; i < operands.size(); i++)
		thunkCaller += "\t" + thunkName + "->" + operands[i] + " = &" + operands[i] + ";\n";

	callString = ",gthunk_" + funcName + "_" + to_string(thunk_id);
	thunkCaller += "\t";
}

void createThunkForVariable(FunctionDecl *f, int index, Expr *expr, string str, string &thunkBody,
				string &thunkStruct, string &thunkCaller, string &callString)
{
	string funcName = f->getNameInfo().getName().getAsString();
	vector <string> operands;
	collectExpressionVariable(expr, operands);
	createThunkStruct(operands, thunkStruct);
	string returnType = getTypeFromOperands(operands);
	currentRetType = returnType;
	//thunkBody += "int* gthunk_" + funcName + "_" + to_string(thunk_id);
	thunkBody += returnType + "* gthunk_" + funcName + "_" + to_string(thunk_id);
	thunkBody += "()\n{\n\t";
	/* we should have just one operand here */
	//rewriteExpressionForThunk(str, operands);
	assert(operands.size() == 1);
	str = "ptr_thunk_" + to_string(thunk_id) + "->" + operands[0] + ";\n";
	thunkBody += "return  " + str + "}\n\n";
	string thunkName = "ptr_thunk_"+ to_string(thunk_id);
	thunkCaller = thunkName + " = malloc(sizeof(struct struct_thunk_" + to_string(thunk_id) + "));\n";
	for (int i = 0; i < operands.size(); i++)
		thunkCaller += "\t" + thunkName + "->" + operands[i] + " = &" + operands[i] + ";\n";

	thunkCaller += "\t";

	if (isLocalVariable(f, operands[0])) {
		callString = ", gthunk_" + funcName + "_" + to_string(thunk_id);
	} else {
		int pos = findArgumentIndex(f, operands[0]);
		string funcName = f->getNameInfo().getName().getAsString();
		callString = ", thunk_" + funcName + "_" + to_string(pos);
	}
}

void createThunkFromExpression(FunctionDecl *f, int index, Expr *expr, string str, string &thunkBody,
			string &thunkStruct, string &thunkCaller, string &callString, bool isExpression)
{
	if (isExpression)
		createThunkForExpression(f, index, expr, str, thunkBody, thunkStruct, thunkCaller, callString);
	else
		createThunkForVariable(f, index, expr, str, thunkBody, thunkStruct, thunkCaller, callString);

	//cout << thunkCaller << "\n";
}


void insertThunkDefinition(FunctionDecl *f, int index, string thunk)
{
	/* make sure we do not rewrite more than once */
	string funcName = f->getNameInfo().getName().getAsString();
	string key = funcName + to_string(index);
	//if (findProcessedEntry(key))
	//	return;

	SourceRange sr = f->getSourceRange();
	SourceLocation loc = sr.getBegin();
	TheRewriter.InsertTextBefore(loc, thunk);
}

void insertThunkInitialization(FunctionDecl *f, Expr *expr, int index, string thunk, Expr *source)
{
	/* make sure we do not rewrite more than once */
	string funcName = f->getNameInfo().getName().getAsString();
	string key = funcName + to_string(index);
	//if (findProcessedEntry(key))
	//	return;

	SourceLocation loc = source->getExprLoc();
	TheRewriter.InsertTextBefore(loc, thunk);
}

void updateCurrentRetType(FunctionDecl *f, string arg)
{
	arg = arg.substr(0, arg.find("["));
	isLocalVariable(f, arg);
	string type = typeMap.find(arg)->second;
	currentRetType = type;
}

void insertThunkCalleeDeclaration(FunctionDecl *f, Expr *expr, string arg, int index, bool isExpression)
{
	/* make sure we do not rewrite more than once */
	string funcName = f->getNameInfo().getName().getAsString();
	string key = funcName + to_string(index);
	if (findProcessedEntry(key))
		return;

	int count = 0;
	MutableArrayRef<ParmVarDecl *> params = f->parameters();
	MutableArrayRef<ParmVarDecl *>::iterator ptr;
	MutableArrayRef<ParmVarDecl *>::iterator end;
	ParmVarDecl *par;
	for (ptr = params.begin(), end = params.end(); ptr != end; ptr = ptr + 1) {
		par = *ptr;
		if (count == index)
			break;
		count++;
	}
	const NamedDecl *namedDecl = dyn_cast<NamedDecl>(par);
	vector <string> operands;
	if (isa<BinaryOperator>(expr))
		collectExpressionOperands(cast<BinaryOperator>(expr), operands);
	else
		collectExpressionVariable(expr, operands);
	string type = getTypeFromOperands(operands);
	if (type.compare("") == 0)
		type = "int";
	currentRetType = type;
	string str = ", " + currentRetType + "* thunk_" + funcName + "_" + to_string(index) + "()";
	SourceRange sr = namedDecl->getSourceRange();
	SourceLocation loc = sr.getEnd();
	TheRewriter.InsertTextAfterToken(loc, str);
}

void updateDeclarationType(Decl *d, string var)
{
	NamedDecl *nd = cast<NamedDecl>(d);
	ValueDecl *vd = cast<ValueDecl>(nd);
	QualType qt = vd->getType();
	string qt_str = qt.getAsString();

	if(var.find("[") != string::npos) {
		string arrName = var.substr(0, var.find("["));
		string type = qt_str.substr(0, qt_str.find("[") - 1);
		typeMap[arrName] = type;
		return;
	}
	typeMap[var] =  qt_str.substr(0, qt_str.find("[") - 1);
}

bool matchDeclarationName(Decl *d, string var)
{
	NamedDecl *nd = cast<NamedDecl>(d);
	string name = nd->getNameAsString();
	updateDeclarationType(d, name);
	if (name.compare(var) == 0) {
		return true;
	}
	return false;
}

bool checkLocalDeclaration(DeclStmt *decl, string var)
{
	if (decl->isSingleDecl()) {
		Decl *d = decl->getSingleDecl();
		return matchDeclarationName(d, var);
	} else {
		DeclGroupRef DR = decl->getDeclGroup();
		for (DeclGroupRef::iterator b = DR.begin(), e = DR.end(); b != e; ++b)
		{
			Decl *d = *b;
			if(matchDeclarationName(d, var))
				return true;
		}
	}
	return false;
}

bool isLocalVariable(FunctionDecl *f, string var)
{
	bool found = false;
	visitFunctionBody(f, var, &found);
	if (found)
		return true;
	return false;
}

bool isLocalArray(FunctionDecl *f, string var)
{
	if (var.find("[") != string::npos)
		return true;
	else
		return false;
}

bool isLocalStruct(FunctionDecl *f, string var)
{
	bool found;
	string type;
	visitFunctionBody(f, var, &found);
	type = typeMap.find(var)->second;
	currentRetType = type;
	if (type.find("struct") != string::npos)
		return true;
	return false;
}

void replaceExpressionText(Expr *expr)
{
	SourceRange sr = expr->getSourceRange();
	SourceManager &sm = TheRewriter.getSourceMgr();
	const LangOptions &opt = TheRewriter.getLangOpts();
	CharSourceRange range = CharSourceRange::getTokenRange(sr.getBegin(), sr.getEnd());
	const string text = Lexer::getSourceText(range, sm, opt);
	if (isConstant(text))
		return;
	string text2 = text + "_thunk";
	//llvm::outs() << "replacing " << text << " with " << text2 << "\n";
	TheRewriter.ReplaceText(sr, text2);
}

void printExpressionText(Expr *expr)
{
	SourceRange sr = expr->getSourceRange();
	SourceManager &sm = TheRewriter.getSourceMgr();
	const LangOptions &opt = TheRewriter.getLangOpts();
	CharSourceRange range = CharSourceRange::getTokenRange(sr.getBegin(), sr.getEnd());
	const string text = Lexer::getSourceText(range, sm, opt);
	//cout << "expression: " << text << "\n";
}

void printStatementText(Stmt *s)
{
	SourceRange sr = s->getSourceRange();
	SourceManager &sm = TheRewriter.getSourceMgr();
	const LangOptions &opt = TheRewriter.getLangOpts();
	bool invalid;
	CharSourceRange range = CharSourceRange::getTokenRange(sr.getBegin(), sr.getEnd());
	StringRef str = Lexer::getSourceText(range, sm, opt, &invalid);
	//llvm::outs() << str << "\n";
}

bool emptyDeclarationText(Decl *s)
{
	SourceRange sr = s->getSourceRange();
	SourceManager &sm = TheRewriter.getSourceMgr();
	const LangOptions &opt = TheRewriter.getLangOpts();
	bool invalid;
	CharSourceRange range = CharSourceRange::getTokenRange(sr.getBegin(), sr.getEnd());
	StringRef str = Lexer::getSourceText(range, sm, opt, &invalid);
	llvm::outs() << "declaration: " << str << "abcd\n";
	return (str.compare("") == 0);
}

void visitBinaryOperation(BinaryOperator *bop)
{
	Expr *lhs = bop->getLHS();
	Expr *rhs = bop->getRHS();
	printExpressionText(lhs);
	printExpressionText(rhs);
	if (!isa<BinaryOperator>(lhs))
		replaceExpressionText(lhs);
	else
		visitBinaryOperation(cast<BinaryOperator>(lhs));

	if (!isa<BinaryOperator>(rhs))
		replaceExpressionText(rhs);
	else
		visitBinaryOperation(cast<BinaryOperator>(rhs));
}

void replaceExpressionVariable(Expr *expr, string src, string dst)
{
	SourceRange sr = expr->getSourceRange();
	SourceManager &sm = TheRewriter.getSourceMgr();
	const LangOptions &opt = TheRewriter.getLangOpts();
	CharSourceRange range = CharSourceRange::getTokenRange(sr.getBegin(), sr.getEnd());
	const string text = Lexer::getSourceText(range, sm, opt);
	//llvm::outs() << "comparing " << text << " with " << src << "\n";
	if (text[0] == '*') {
		dst = "(*" + dst + ")";
		TheRewriter.ReplaceText(sr, dst);
		return;
	}

	if (text.compare(src) == 0) {
		TheRewriter.ReplaceText(sr, dst);
		return;
	}
	/* special handling for structs */
	string prefix = src + ".";
	string postfix = text.substr(text.find(".") + 1);
	if (strncmp(text.c_str(), prefix.c_str(), prefix.size()) == 0) {
		dst += "." + postfix;
		TheRewriter.ReplaceText(sr, dst);
	}
}

void rewriteBinaryOperationVariable(BinaryOperator *bop, string src, string dst)
{
	Expr *lhs = bop->getLHS();
	Expr *rhs = bop->getRHS();
	if (!isa<BinaryOperator>(lhs))
		replaceExpressionVariable(lhs, src, dst);
	else
		rewriteBinaryOperationVariable(cast<BinaryOperator>(lhs), src, dst);

	if (!isa<BinaryOperator>(rhs))
		replaceExpressionVariable(rhs, src, dst);
	else
		rewriteBinaryOperationVariable(cast<BinaryOperator>(rhs), src, dst);
}

void rewriteSimpleStatement(FunctionDecl *f, Stmt *s, string src, string dst)
{
	if (isa<BinaryOperator>(s))
		rewriteBinaryOperationVariable(cast<BinaryOperator>(s), src, dst);
}

void rewriteCompoundStatement(FunctionDecl *f, Stmt *s, string src, string dst)
{
	CompoundStmt *cs = cast<CompoundStmt>(s);
	
	for(CompoundStmt::body_iterator ptr = cs->body_begin(),
	end = cs->body_end(); ptr != end; ptr++) {
		Stmt *stmt = *ptr;
		if (isa<CompoundStmt>(stmt))
			rewriteCompoundStatement(f, stmt, src, dst);
		else {
			if (isa<IfStmt>(stmt))
				rewriteIfStatement(f, cast<IfStmt>(stmt), src, dst);
			else
				rewriteSimpleStatement(f, stmt, src, dst);
		}
	}
}

void rewriteIfStatement(FunctionDecl *f, IfStmt *s, string src, string dst)
{
	Expr *expr = s->getCond();
	if (isa<BinaryOperator>(expr))
		rewriteBinaryOperationVariable(cast<BinaryOperator>(expr), src, dst);
	else
		replaceExpressionVariable(expr, src, dst);

	Stmt *Then = s->getThen();
	if (!Then)
		goto elseLabel;

	if (isa<CompoundStmt>(Then))
		rewriteCompoundStatement(f, Then, src, dst);
	else
		rewriteSimpleStatement(f, Then, src, dst);

elseLabel:
	Stmt *Else = s->getElse();
	if (!Else)
		return;

	if (isa<CompoundStmt>(Else)) {
		//cout << "else has a compound stmt\n";
		rewriteCompoundStatement(f, Else, src, dst);
	} else if (isa<IfStmt>(Else)) {
		rewriteIfStatement(f, cast<IfStmt>(Else), src, dst);
	} else {
		rewriteSimpleStatement(f, Else, src, dst);
	}
}

void rewriteFunctionVariable(FunctionDecl *f, string src, string dst)
{
	Stmt *fBody = f->getBody();
	if(isa<CompoundStmt>(fBody))
		rewriteCompoundStatement(f, fBody, src, dst);
	else
		rewriteSimpleStatement(f, fBody, src, dst);
}

void rewriteFunctionBodyAsThunk(FunctionDecl *f, int index, bool isExpression)
{
	/* make sure the corresponding argument has not been rewritten before */
	string funcName = f->getNameInfo().getName().getAsString();
	string key = funcName + to_string(index);
	if(findProcessedEntry(key))
		return;

	if (f->hasBody()) {
		Stmt *FuncBody = f->getBody();
		int numParams = f->getNumParams();
		int count = 0;
		/* modify parameters */
		MutableArrayRef<ParmVarDecl *> params = f->parameters();
		MutableArrayRef<ParmVarDecl *>::iterator ptr;
		MutableArrayRef<ParmVarDecl *>::iterator end;
		ParmVarDecl *par;
		for (ptr = params.begin(), end = params.end(); ptr != end; ptr = ptr + 1) {
			par = *ptr;
			if (count == index)
				break;
			count++;
		}
		const NamedDecl *namedDecl = dyn_cast<NamedDecl>(par);
		string src = namedDecl->getNameAsString();
		string dst = "(*(thunk_" + funcName + "_" + to_string(index) + "()))";
		/*
 		if (isExpression)
			dst = "(thunk_" + funcName + "_" + to_string(index) + "())";
		else
			dst = "(*(thunk_" + funcName + "_" + to_string(index) + "()))";
		*/
		//cout << "rewriting " << src << " as " << dst << "\n";
		rewriteFunctionVariable(f, src, dst);
	}
}

void rewriteFunctionArgument(FunctionDecl *f, int index)
{
	DeclarationName DeclName = f->getNameInfo().getName();
	std::string FuncName = DeclName.getAsString();
	string key = FuncName + to_string(index);
	if (findProcessedEntry(key))
		return;

	int count = 0;
	MutableArrayRef<ParmVarDecl *> params = f->parameters();
	MutableArrayRef<ParmVarDecl *>::iterator ptr;
	MutableArrayRef<ParmVarDecl *>::iterator end;
	ParmVarDecl *par;
	for (ptr = params.begin(), end = params.end(); ptr != end; ptr++) {
		par = *ptr;
		if (count == index)
			break;
		count++;
	}
	const NamedDecl *namedDecl = dyn_cast<NamedDecl>(par);
	string src = namedDecl->getNameAsString();
	StringRef star = "*";
	SourceRange sr = namedDecl->getSourceRange();
	SourceLocation loc = sr.getBegin();
	TheRewriter.InsertTextAfterToken(loc, star);
	string dst = "*" + src;
	//cout << "rewriting " << src << " to " << dst << "\n";
	rewriteFunctionVariable(f, src, dst);
	/* insert into processed map */
	insertProcessedEntry(key);
}

void markFunctionParameterProcessed(FunctionDecl *f, int index)
{
	string key = f->getNameInfo().getName().getAsString();
	key += to_string(index);
	insertProcessedEntry(key);
}

int findNextOperator(string str)
{
	int index = 100, m = 100;
	if (str.find('+') != string::npos)
		m = min(m, int(str.find('+')));
	if (str.find('-') != string::npos)
		m = min(m, int(str.find('-')));
	if (str.find('*') != string::npos)
		m = min(m, int(str.find('*')));
	if (str.find("/") != string::npos)
		m = min(m, int(str.find("/")));
	if (str.find("&&") != string::npos)
		m = min(m, int(str.find("&&")));
	if (str.find("||") != string::npos)
		m = min(m, int(str.find("||")));
	if (m == 100)
		return -1;
	return m;
}
// Function to remove all spaces from a given string
void makeTokens(string str, vector<string> &vec, vector<string> &vec2)
{
	str.erase(remove(str.begin(), str.end(), ' '), str.end());
	str.erase(remove(str.begin(), str.end(), '('), str.end());
	str.erase(remove(str.begin(), str.end(), ')'), str.end());
	while(true) {
		int index = findNextOperator(str);
		if (index == -1) {
			vec2.push_back(str);
			if (!isConstant(str))
				vec.push_back(str);
			break;
		}
		string tmp = str.substr(0, index);
		vec2.push_back(tmp);
		if(!isConstant(tmp))
			vec.push_back(tmp);
		str = str.substr(index+1);
	}
}

void createThunkFromArrayExpression(FunctionDecl *f, int index, string arrName, string arrArg, string &thunkBody,
			string &thunkStruct, string &thunkCaller, string &callString)
{
	string funcName = f->getNameInfo().getName().getAsString();
	vector <string> operands1, operands2;
	operands1.push_back(arrName);
	/* hack */
	isLocalVariable(f, arrName);
	makeTokens(arrArg, operands1, operands2);
	/* hack */
	for (int i = 0; i < operands1.size(); i++)
		isLocalVariable(f, operands1[i]);

	createThunkStruct(operands1, thunkStruct);
	string type = typeMap.find(arrName)->second;
	currentRetType = type;
	thunkBody += type + "* gthunk_" + funcName + "_" + to_string(thunk_id);
	thunkBody += "()\n{\n\t";
	string str = arrArg;
	rewriteExpressionForThunk(str, operands2);
	thunkBody += "return ptr_thunk_" + to_string(thunk_id) + "->" + arrName + " + ";
	thunkBody += str + ";\n}\n\n";
	//thunkBody += "int tmp;\n\ttmp = " + str + ";\n\treturn tmp;\n}\n\n";
	//cout << thunkBody << "\n"; 
	/* increment the global counter */
	string thunkName = "ptr_thunk_"+ to_string(thunk_id);
	thunkCaller = thunkName + " = malloc(sizeof(struct struct_thunk_" + to_string(thunk_id) + "));\n";
	for (int i = 0; i < operands1.size(); i++) {
		if (operands1[i].compare(arrName) != 0)
			thunkCaller += "\t" + thunkName + "->" +
					operands1[i] + " = &" + operands1[i] + ";\n";
		else
			thunkCaller += "\t" + thunkName + "->" +
				operands1[i] + " = " + operands1[i] + ";\n";
	}

	callString = ",gthunk_" + funcName + "_" + to_string(thunk_id);
	thunkCaller += "\t";
	//cout << thunkBody << "\n";
	//cout << thunkCaller << "\n";
	//cout << thunkStruct << "\n";
}

void createThunkForStruct(FunctionDecl *f, int index, string var, string &thunkBody,
		string &thunkStruct, string &thunkCaller, string &callString)
{
	string funcName = f->getNameInfo().getName().getAsString();
	vector <string> operands;
	operands.push_back(var);
	createThunkStruct(operands, thunkStruct);
	string returnType = getTypeFromOperands(operands);
	currentRetType = returnType;
	//thunkBody += "int* gthunk_" + funcName + "_" + to_string(thunk_id);
	thunkBody += returnType + "* gthunk_" + funcName + "_" + to_string(thunk_id);
	thunkBody += "()\n{\n\t";
	/* we should have just one operand here */
	//rewriteExpressionForThunk(str, operands);
	assert(operands.size() == 1);
	string str = "ptr_thunk_" + to_string(thunk_id) + "->" + operands[0] + ";\n";
	thunkBody += "return  " + str + "}\n\n";
	string thunkName = "ptr_thunk_"+ to_string(thunk_id);
	thunkCaller = thunkName + " = malloc(sizeof(struct struct_thunk_" + to_string(thunk_id) + "));\n";
	for (int i = 0; i < operands.size(); i++)
		thunkCaller += "\t" + thunkName + "->" + operands[i] + " = &" + operands[i] + ";\n";

	thunkCaller += "\t";

	if (isLocalVariable(f, operands[0])) {
		callString = ", gthunk_" + funcName + "_" + to_string(thunk_id);
	} else {
		int pos = findArgumentIndex(f, operands[0]);
		string funcName = f->getNameInfo().getName().getAsString();
		callString = ", thunk_" + funcName + "_" + to_string(pos);
	}
}

void handleCallExpression(FunctionDecl *f, CallExpr *expr, Expr *insertLoc, bool *found)
{
	/* 
	 * while matching variable declarations, simply return to avoid
	 * recursive checking
	 */
	if (found != NULL)
		return;

	int nr_args = expr->getNumArgs();
	if (nr_args == 0)
		return;

	bool invalid;
	StringRef str;
	CharSourceRange range;
	SourceManager &sm = TheRewriter.getSourceMgr();
	const LangOptions &opt = TheRewriter.getLangOpts();
	string callString = "";
	string comma = ", ";
	for (int i = 0; i < nr_args; i++) {
		Expr *arg = expr->getArg(i);
		SourceLocation loc = arg->getBeginLoc();
		range = CharSourceRange::getTokenRange(arg->getBeginLoc(), arg->getEndLoc());
		str = Lexer::getSourceText(range, sm, opt, &invalid);
		if (isConstant(str)) continue;
		/*TODO: dummy call to get variable type */
		isLocalVariable(f, str);
		if (isLocalStruct(f, str)) {
			string thunkBody, thunkStruct, thunkCaller, callString;
			createThunkForStruct(f, i, str, thunkBody, thunkStruct,
                                        thunkCaller, callString);
			FunctionDecl *callee = expr->getDirectCallee();
			string thunkText = thunkStruct + thunkBody;
			insertThunkDefinition(callee, i, thunkText);
			insertThunkInitialization(callee, expr, i, thunkCaller, insertLoc);
			rewriteFunctionBodyAsThunk(callee, i, false);
			loc = arg->getEndLoc();
			TheRewriter.InsertTextAfterToken(loc, callString);
			insertThunkCalleeDeclaration(callee, arg, str, i, false);
			markFunctionParameterProcessed(callee, i);
			thunk_id++;
		} else if (!isLocalArray(f, str)) {
			/* thunk return type depends on whether argument is an expression */
			bool isExpression = isa<BinaryOperator>(arg);
			string thunkBody, thunkStruct, thunkCaller, callString;
			createThunkFromExpression(f, i, arg, str, thunkBody, thunkStruct,
					thunkCaller, callString, isExpression);
			FunctionDecl *callee = expr->getDirectCallee();
			string thunkText = thunkStruct + thunkBody;
			/* avoid spurious thunk generation */
			if(isLocalVariable(f, str) || isa<BinaryOperator>(arg)) {
				insertThunkDefinition(callee, i, thunkText);
				insertThunkInitialization(callee, expr, i, thunkCaller, insertLoc);
				thunk_id++;
			}
			rewriteFunctionBodyAsThunk(callee, i, isExpression);
			loc = arg->getEndLoc();
			TheRewriter.InsertTextAfterToken(loc, callString);
			//llvm::outs() << "argument: " << str << "\n";
			insertThunkCalleeDeclaration(callee, arg, str, i, isExpression);
			markFunctionParameterProcessed(callee, i);
		} else {
			string param = str;
			string arrName = param.substr(0, param.find("["));
			isLocalVariable(f, arrName);
			currentRetType = typeMap.find(arrName)->second;
			int len = str.find("]") - str.find("[") - 1;
			string subScript = param.substr(param.find("[") + 1, len);
			string thunkBody, thunkStruct, thunkCaller, callString;
			createThunkFromArrayExpression(f, i, arrName, subScript, thunkBody,
					thunkStruct, thunkCaller, callString);
			FunctionDecl *callee = expr->getDirectCallee();
			string thunkText = thunkStruct + thunkBody;
			insertThunkDefinition(callee, i, thunkText);
			insertThunkInitialization(callee, expr, i, thunkCaller, insertLoc);
			loc = arg->getEndLoc();
			rewriteFunctionBodyAsThunk(callee, i, false);
			TheRewriter.InsertTextAfterToken(loc, callString);
			insertThunkCalleeDeclaration(callee, arg, str, i, false);
			markFunctionParameterProcessed(callee, i);
			thunk_id++;
		}
	}
}

void visitSimpleStatement(FunctionDecl *f, Stmt *s, string var, bool *found)
{
	if (isa<CallExpr>(s)) {
		handleCallExpression(f, cast<CallExpr>(s), cast<Expr>(s), found);
	}
	else if (isa<BinaryOperator>(s)) {
		BinaryOperator *bop = cast<BinaryOperator>(s);
		Expr *lhs = bop->getLHS();
		Expr *rhs = bop->getRHS();
		if (isa<CallExpr>(rhs))
			handleCallExpression(f, cast<CallExpr>(rhs), cast<Expr>(lhs), found);
	}
	else {
		if (isa<DeclStmt>(s) && found != NULL) {
			if(checkLocalDeclaration(cast<DeclStmt>(s), var)) 
				*found = true;
		} else {
			if (found == NULL)
				printStatementText(s);
		}
	}
}

void visitCompoundStatement(FunctionDecl *f, Stmt *s, string var, bool *found)
{
	CompoundStmt *cs = cast<CompoundStmt>(s);
	
	for(CompoundStmt::body_iterator ptr = cs->body_begin(),
	end = cs->body_end(); ptr != end; ptr++) {
		Stmt *stmt = *ptr;
		if (isa<CompoundStmt>(stmt)) 
			visitCompoundStatement(f, stmt, var, found);
		else {
			if (isa<IfStmt>(stmt))
				visitIfStatement(f, cast<IfStmt>(stmt), var, found);
			else if (isa<ForStmt>(stmt))
				visitForStatement(f, cast<ForStmt>(stmt), var, found);
			else {
				visitSimpleStatement(f, stmt, var, found);
			}
		}
	}
}

void visitForStatement(FunctionDecl *f, ForStmt *s, string var, bool *found)
{
	Stmt *forBody = s->getBody();
	if (isa<CompoundStmt>(forBody))
		visitCompoundStatement(f, forBody, var, found);
	else
		visitSimpleStatement(f, forBody, var, found);
}

void visitIfStatement(FunctionDecl *f, IfStmt *s, string var, bool *found)
{
	Expr *expr = s->getCond();
	if (found == NULL)
		printExpressionText(expr);
	Stmt *Then = s->getThen();
	if(!Then)
		goto elseLabel;

	if (isa<CompoundStmt>(Then))
		visitCompoundStatement(f, Then, var, found);
	else
		visitSimpleStatement(f, Then, var, found);

elseLabel:
	Stmt *Else = s->getElse();
	if (!Else)
		return;

	if (isa<CompoundStmt>(Else)) 
		visitCompoundStatement(f, Else, var, found);
	else if (isa<IfStmt>(Else))
		visitIfStatement(f, cast<IfStmt>(Else), var, found);
	else
		visitSimpleStatement(f, Else, var, found);
}


void visitFunctionBody(FunctionDecl *f, string var, bool *found)
{
	Stmt *fBody = f->getBody();
	if(isa<CompoundStmt>(fBody)) {
		visitCompoundStatement(f, fBody, var, found);
	}
	else {
		visitSimpleStatement(f, fBody, var, found);
	}
}

bool VisitFunctionDecl(FunctionDecl *f)
{
	SourceManager &sm = TheRewriter.getSourceMgr();
	const LangOptions &lo = TheRewriter.getLangOpts();
#if 0
	if (f->hasBody()) {
		SourceRange sr = f->getSourceRange();
		CharSourceRange range = CharSourceRange::getTokenRange(sr.getBegin(), sr.getEnd());
		const string text = Lexer::getSourceText(range, sm, lo);
		Stmt *FuncBody = f->getBody();
		int numParams = f->getNumParams();
		/* modify parameters */
		MutableArrayRef<ParmVarDecl *> params = f->parameters();
		bool invalid;
		MutableArrayRef<ParmVarDecl *>::iterator ptr;
		MutableArrayRef<ParmVarDecl *>::iterator end;
		for (ptr = params.begin(), end = params.end(); ptr != end; ptr = ptr + 1) {
			ParmVarDecl *par = *ptr;
			const NamedDecl *namedDecl = dyn_cast<NamedDecl>(par);
			//cout << "name: " << namedDecl->getNameAsString() << "\n";
			StringRef star = "xyz";
			SourceRange sr = namedDecl->getSourceRange();
			SourceLocation loc = sr.getEnd();
			//TheRewriter.InsertTextAfterToken(loc, star);
		}

		QualType QT = f->getReturnType();
		std::string TypeStr = QT.getAsString();

		DeclarationName DeclName = f->getNameInfo().getName();
		std::string FuncName = DeclName.getAsString();

		std::stringstream SSBefore;
		SSBefore << "// Begin function " << FuncName << " returning " << TypeStr << "\n";
		SourceLocation ST = f->getSourceRange().getBegin();
		std::stringstream SSAfter;
		SSAfter << "\n// End function " << FuncName;
		ST = FuncBody->getEndLoc().getLocWithOffset(1);

		visitFunctionBody(f, "", NULL);
	}
#endif
	if (f->hasBody()) {
		Stmt *FuncBody = f->getBody();
		int numParams = f->getNumParams();

		MutableArrayRef<ParmVarDecl *> params = f->parameters();
		bool invalid;
		MutableArrayRef<ParmVarDecl *>::iterator ptr;
		MutableArrayRef<ParmVarDecl *>::iterator end;

		for (ptr = params.begin(), end = params.end(); ptr != end; ptr = ptr + 1) {
			ParmVarDecl *par = *ptr;
			const NamedDecl *namedDecl = dyn_cast<NamedDecl>(par);
			StringRef star = "*";
			SourceRange sr = namedDecl->getSourceRange();
			SourceLocation loc = sr.getBegin();
		}

		// Type name as string
		QualType QT = f->getReturnType();
		std::string TypeStr = QT.getAsString();

		// Function name
		DeclarationName DeclName = f->getNameInfo().getName();
		std::string FuncName = DeclName.getAsString();

		std::stringstream SSBefore;
		//SSBefore << "// Begin function " << FuncName << " returning " << TypeStr << "\n";
		SSBefore << "";
		SourceLocation ST = f->getSourceRange().getBegin();
		TheRewriter.InsertText(ST, SSBefore.str(), true, true);
		std::stringstream SSAfter;
		//SSAfter << "\n// End function " << FuncName;
		SSAfter << "";
		ST = FuncBody->getEndLoc().getLocWithOffset(1);
		TheRewriter.InsertText(ST, SSAfter.str(), true, true);
		visitFunctionBody(f, "", NULL);
	}
	insertHeaderFiles();
	return true;
}

private:
	Rewriter &TheRewriter;
};

class MyASTConsumer : public ASTConsumer {
public:
	MyASTConsumer(Rewriter &R) : Visitor(R) {}

	virtual bool HandleTopLevelDecl(DeclGroupRef DR) {
		for (DeclGroupRef::iterator b = DR.begin(), e = DR.end(); b != e; ++b) {
			Visitor.TraverseDecl(*b);
		}
		return true;
	}

private:
	MyASTVisitor Visitor;
};

int main(int argc, char *argv[]) {
	if (argc != 2) {
		llvm::errs() << "Usage: rewritersample <filename>\n";
		return 1;
	}

	// CompilerInstance will hold the instance of the Clang compiler for us,
	// managing the various objects needed to run the compiler.
	CompilerInstance TheCompInst;
	TheCompInst.createDiagnostics();
  
	LangOptions &lo = TheCompInst.getLangOpts();
	lo.CPlusPlus = 1;

	// Initialize target info with the default triple for our platform.
	auto TO = std::make_shared<TargetOptions>();
	TO->Triple = llvm::sys::getDefaultTargetTriple();
	TargetInfo *TI =
	TargetInfo::CreateTargetInfo(TheCompInst.getDiagnostics(), TO);
	TheCompInst.setTarget(TI);

	TheCompInst.createFileManager();
	FileManager &FileMgr = TheCompInst.getFileManager();
	TheCompInst.createSourceManager(FileMgr);
	SourceManager &SourceMgr = TheCompInst.getSourceManager();
	TheCompInst.createPreprocessor(TU_Module);
	TheCompInst.createASTContext();

	// A Rewriter helps us manage the code rewriting task.
	Rewriter TheRewriter;
	TheRewriter.setSourceMgr(SourceMgr, TheCompInst.getLangOpts());

	// Set the main file handled by the source manager to the input file.
	const FileEntry *FileIn = FileMgr.getFile(argv[1]);
	SourceMgr.setMainFileID(
	SourceMgr.createFileID(FileIn, SourceLocation(), SrcMgr::C_User));
	TheCompInst.getDiagnosticClient().BeginSourceFile(
	TheCompInst.getLangOpts(), &TheCompInst.getPreprocessor());

	// Create an AST consumer instance which is going to get called by ParseAST.
	MyASTConsumer TheConsumer(TheRewriter);

	// Parse the file to AST, registering our consumer as the AST consumer.
	ParseAST(TheCompInst.getPreprocessor(), &TheConsumer,
	TheCompInst.getASTContext());

	// At this point the rewriter's buffer should be full with the rewritten file contents.
	const RewriteBuffer *RewriteBuf =
	TheRewriter.getRewriteBufferFor(SourceMgr.getMainFileID());
	//llvm::outs() << "\n\n";
	//llvm::outs() << std::string(RewriteBuf->begin(), RewriteBuf->end());

	/* write to file */
	string outName = "output.c";
	error_code OutErrorInfo;
	llvm::raw_fd_ostream outFile(llvm::StringRef(outName), OutErrorInfo, llvm::sys::fs::F_None);
	outFile << std::string(RewriteBuf->begin(), RewriteBuf->end());
	return 0;
}
