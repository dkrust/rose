#include "sage3basic.h"
#include "TFTypeTransformer.h"
#include "SgNodeHelper.h"
#include "AstTerm.h"
#include "AstMatching.h"
#include "CppStdUtilities.h"
#include "TFToolConfig.h"
#include "TFHandles.h"
using namespace std;

namespace SgNodeHelper {

bool isTypeBasedOn(SgType * type, SgType * base, bool strip_type = false) {
  ROSE_ASSERT(type != NULL);
  ROSE_ASSERT(base != NULL);

  if (strip_type) {
    type = type->stripType(
      SgType::STRIP_ARRAY_TYPE     |
      SgType::STRIP_POINTER_TYPE   |
      SgType::STRIP_MODIFIER_TYPE  |
      SgType::STRIP_REFERENCE_TYPE |
      SgType::STRIP_RVALUE_REFERENCE_TYPE
    );
  }

  if (type == base) return true;

  SgTypedefType * td_type = isSgTypedefType(type);
  if (td_type != NULL) {
    return isTypeBasedOn(td_type->get_base_type(), base, strip_type);
  } else {
    return false;
  }
}

}

namespace Typeforge {

// static member
bool TFTypeTransformer::_traceFlag=false;

//Constructors for directive list 
TransformDirective::TransformDirective(bool transformBase, bool onlyList, SgType* to_type){
  base = transformBase; listing = onlyList; toType = to_type;
}

NameTransformDirective::NameTransformDirective(string varName, SgFunctionDeclaration* functionDeclaration, bool base, bool listing, SgType* toType) : TransformDirective(base, listing, toType){
  name = varName; funDecl = functionDeclaration;
}

TypeTransformDirective::TypeTransformDirective(string functionLocation, SgFunctionDeclaration* functionDeclaration, SgType* from_type, bool base, bool listing, SgType* toType) : TransformDirective(base, listing, toType){
  location = functionLocation; funDecl = functionDeclaration; fromType = from_type;
}

HandleTransformDirective::HandleTransformDirective(SgNode* handleNode, bool base, bool listing, SgType* toType) : TransformDirective(base, listing, toType){
  node = handleNode;
}

FileTransformDirective::FileTransformDirective(string file) : TransformDirective(false, false, nullptr){
  fileName = file;
}


SetTransformDirective::SetTransformDirective(bool value) : TransformDirective(false, false, nullptr){
  flag = value;
}

//Methods for adding to directive list
void TFTypeTransformer::addHandleTransformationToList(list<VarTypeVarNameTuple>& list,SgType* type,bool base,SgNode* handleNode, bool listing){
  list.insert(list.begin(),new HandleTransformDirective(handleNode, base, listing, type));
} 

void TFTypeTransformer::addTypeTransformationToList(list<VarTypeVarNameTuple>& list,SgType* toType, SgFunctionDeclaration* funDecl, string varNames, bool base, SgType* fromType, bool listing){
  list.push_back(new TypeTransformDirective(varNames, funDecl, fromType, base, listing, toType));  
}
 
void TFTypeTransformer::addNameTransformationToList(list<VarTypeVarNameTuple>& list,SgType* type, SgFunctionDeclaration* funDecl, string varNames, bool base, bool listing){
  vector<string> varNamesVector=CppStdUtilities::splitByComma(varNames);
  for (auto name:varNamesVector) {
    list.push_back(new NameTransformDirective(name, funDecl, base, listing, type));
  }
}

void TFTypeTransformer::addFileChangeToList(list<VarTypeVarNameTuple>& list, string file){
  list.push_back(new FileTransformDirective(file));
}

void TFTypeTransformer::addSetChangeToList(list<VarTypeVarNameTuple>& list, bool flag){
  list.push_back(new SetTransformDirective(flag));
}

//Methods to run directive list
int NameTransformDirective::run(SgProject* project, TFTypeTransformer* tt) {
  int changes = tt->changeVariableType(project, funDecl, name, toType, base, nullptr, listing);

  if(changes == 0) {
    cout << "Warning: Did not find variable " << name;
    if(funDecl) {
      cout<<" in function " << funDecl->get_name() << "." << endl;
    } else {
      cout << " in globals." << endl;
    }
  } else if(changes > 1) {
    // cout << "Warning: Found more than one declaration of variable "<<name<<"."<<endl;
  }

  return changes;
}

int TypeTransformDirective::run(SgProject* project, TFTypeTransformer* tt) {
  return tt->changeVariableType(project, funDecl, location, toType, base, fromType, listing);
}

int HandleTransformDirective::run(SgProject* project, TFTypeTransformer* tt){
  return tt->changeHandleType(node, toType, base, listing);
}

int FileTransformDirective::run(SgProject* project, TFTypeTransformer* tt){
  if(fileName == "") return 0;//tt->writeConfig();
  else tt->setConfigFile(fileName);
  return 0;  
}

int SetTransformDirective::run(SgProject* project, TFTypeTransformer* tt){
  tt->changeSetFlag(flag);
  return 0;  
}

//TypeTransformer stores changes during analysis phase then performs the changes when done.
int Transformer::transform(){
  for(auto i = transformations.begin(); i != transformations.end(); i++){
    SgNode* node     = i->first;
    string  location = get<0>(i->second);
    SgType* type     = get<1>(i->second);
    if(SgInitializedName* initName = isSgInitializedName(node)){
      TFTypeTransformer::trace("Execution: Changing variable type @"+location+" to type "+type->unparseToString());

      initName->set_type(type);

    } else if(SgFunctionDeclaration* funDecl = isSgFunctionDeclaration(node)){
      TFTypeTransformer::trace("Execution: Changing return type @"+location+" to type "+type->unparseToString());

      SgFunctionType * old_ftype = funDecl->get_type();
      ROSE_ASSERT(old_ftype != NULL);
      SgFunctionType * new_ftype = SageBuilder::buildFunctionType(type, old_ftype->get_argument_list());
      funDecl->set_type(new_ftype);

    } else if(SgCastExp* cast = isSgCastExp(node)){
//    TFTypeTransformer::trace("Execution: Changing cast type @"+location+" to type "+type->unparseToString());

      SgExprListExp * exprlist = isSgExprListExp(cast->get_parent());
      if (exprlist != NULL) {
        for (auto expr: exprlist->get_expressions()) {
          if (expr == cast) {
            expr = cast->get_operand();
          }
        }
      } else {
        assert(false); // TODO
      }

    } else {
      cerr << "Error: attempted to apply changes to an unknown node " << node->class_name() <<endl;
      std::abort();
    }
  }
  return transformationsCount;
}

/* Proposed filtering:

int Transformer::filter() {
  for(auto i = transformations.begin(); i != transformations.end(); i++){
    SgNode* node     = i->first;
    string  location = get<0>(i->second);
    SgType* type     = get<1>(i->second);
    if(SgInitializedName* initName = isSgInitializedName(node)){
      bool valid = true;
      // TODO
      if (!valid) {
        TFTypeTransformer::trace("Filter: Changing variable type @"+location+" to type "+type->unparseToString());
      }
    } else if(SgFunctionDeclaration* funDecl = isSgFunctionDeclaration(node)){
      bool valid = true;
      // TODO
      if (!valid) {
        TFTypeTransformer::trace("Filter: Changing return type @"+location+" to type "+type->unparseToString());
      }
    } else if(SgCastExp* cast = isSgCastExp(node)){
      bool valid = true;
      // TODO
      if (!valid) {
        TFTypeTransformer::trace("Filter: Changing cast type @"+location+" to type "+type->unparseToString());
      }
    } else {
      cerr << "Error: attempted to apply changes to an unknown node " << node->class_name() <<endl;
      std::abort();
    }
  }
  return transformationsCount;
}*/

int Transformer::addTransformation(string key, SgType* newType, SgNode* node){
  if (!isSgInitializedName(node) && !isSgFunctionDeclaration(node) && !isSgCastExp(node)) {
    cerr << "Error: attempted to apply changes to an unknown node " << node->class_name() <<endl;
    std::abort();
  }
  if(transformations.count(node) != 0){
    return 0;
  } else {
    ReplacementTuple newTuple = make_tuple(key, newType);
    transformations[node] = newTuple;
    transformationsCount++;
    return 1;
  }
}

//Mangae config file if user specifies
void TFTypeTransformer::setConfigFile(string fileName){
  TFToolConfig::open(fileName);
}

void TFTypeTransformer::writeConfig(){
  TFToolConfig::write();
}

//Changes mode to modify sets
bool TFTypeTransformer::changeSetFlag(bool value){
  bool temp = _setFlag;
  _setFlag = value;
  return temp;
}

//Returns variable set associated with the given node
set<SgNode*>* TFTypeTransformer::getSet(SgNode* node, SgType* type){
  type = type->stripType();
  if(typeSets.count(type) == 0){
    Analysis* sets = new Analysis();
    SgProject* project = nullptr;
    SgNode* parent = node;
    while(project == nullptr){
      try{
        parent = SgNodeHelper::getParent(parent);
      }
      catch(...){
        return nullptr;
      }
      project = isSgProject(parent);
    }
    sets->variableSetAnalysis(project, type, true);
    typeSets[type] = sets;
  }
  return typeSets[type]->getSet(node);
}

//Writes varable sets to file
void TFTypeTransformer::writeSets(SgProject* project, SgType* type, string toTypeString){
  type = type->stripType();
  if(typeSets.count(type) == 0){
    Analysis* sets = new Analysis();
    sets->variableSetAnalysis(project, type, true);
    typeSets[type] = sets;
  }
  typeSets[type]->writeAnalysis(type, toTypeString);
}

//Returns the name of the file the specified node is part of
string getNodeFileName(SgNode* node){
  SgNode* currentNode = node;
  SgSourceFile* file = nullptr;
  while(file == nullptr && currentNode != nullptr){
    file = isSgSourceFile(currentNode);
    currentNode = currentNode->get_parent();
  }
  if(currentNode == nullptr) return "";
  else return file->getFileName();
}

//Adds an entry in the config file
void TFTypeTransformer::addToActionList(string varName, string scope, SgType* fromType, SgType* toType, SgNode* handleNode, bool base){
  if(!fromType || !toType || !handleNode) return;
  if(varName == "") return;
  string handle = TFHandles::getAbstractHandle(handleNode);
  if(handle == "") return;
  if(base) TFToolConfig::addChangeVarBaseType(handle, varName, scope, getNodeFileName(handleNode), fromType->unparseToString(), toType->unparseToString()); 
  else TFToolConfig::addChangeVarType(handle, varName, scope, getNodeFileName(handleNode), fromType->unparseToString(), toType->unparseToString()); 
}

void TFTypeTransformer::transformCommandLineFiles(SgProject* project) {
  // make all floating point casts explicit
  makeAllCastsExplicit(project);
  // transform casts in AST
  transformCastsInCommandLineFiles(project);
}


void TFTypeTransformer::transformCommandLineFiles(SgProject* project,VarTypeVarNameTupleList& list) {
  analyzeTransformations(project, list);
  executeTransformations(project);
}

void TFTypeTransformer::analyzeTransformations(SgProject* project, VarTypeVarNameTupleList& list){
  for (auto directive:list) {
    _totalNumChanges += directive->run(project, this);
  }
}


void addExplicitCast(SgProject* project) {
  RoseAst ast(project);
  for (auto n: ast) {
    SgBinaryOp * bop = isSgBinaryOp(n);
    if (bop == NULL) continue;
    SgAssignOp * aop = isSgAssignOp(n);
    SgCompoundAssignOp * caop = isSgCompoundAssignOp(n);
    if (aop != NULL || caop != NULL) {
      SgExpression * lhs = bop->get_lhs_operand_i();
      SgType * lhs_t = lhs->get_type();
      SgType * lhs_t_s = lhs_t->stripType(
        SgType::STRIP_ARRAY_TYPE     |
        SgType::STRIP_POINTER_TYPE   |
        SgType::STRIP_MODIFIER_TYPE  |
        SgType::STRIP_REFERENCE_TYPE |
        SgType::STRIP_RVALUE_REFERENCE_TYPE
      );
      SgExpression * rhs = bop->get_rhs_operand_i();
      SgType * rhs_t = rhs->get_type();
      SgType * rhs_t_s = rhs_t->stripType(
        SgType::STRIP_ARRAY_TYPE     |
        SgType::STRIP_POINTER_TYPE   |
        SgType::STRIP_MODIFIER_TYPE  |
        SgType::STRIP_REFERENCE_TYPE |
        SgType::STRIP_RVALUE_REFERENCE_TYPE
      );
      if (rhs_t_s != lhs_t_s) {
        SgExpression * new_rhs = SageBuilder::buildCastExp(rhs, lhs_t);
        rhs->set_parent(new_rhs);
        new_rhs->set_parent(bop);
        bop->set_rhs_operand_i(new_rhs);
      }
    }
  }
}

void TFTypeTransformer::executeTransformations(SgProject* project){
  _totalNumChanges = _transformer.transform();
//transformCommandLineFiles(project);
  addExplicitCast(project);
}

void TFTypeTransformer::transformCastsInCommandLineFiles(SgProject* project) {
  _castTransformer.transformCommandLineFiles(project);
}

//returns a new type with same structure as root but with newBaseType as a base
SgType* TFTypeTransformer::rebuildBaseType(SgType* root, SgType* newBaseType){
  //handle array type
  if(SgArrayType* arrayType = isSgArrayType(root)){
    SgType* base = rebuildBaseType(arrayType->get_base_type(), newBaseType);
    SgExpression* index = arrayType->get_index();
    SgExprListExp* dim_info = arrayType->get_dim_info();
    if(dim_info != nullptr){
      return SageBuilder::buildArrayType(base, dim_info);
    }
    else if(index != nullptr){
      return SageBuilder::buildArrayType(base, index);
    }
    else{
      return SageBuilder::buildArrayType(base);
    }
  }
  //handle pointer type
  else if(SgPointerType* pointerType = isSgPointerType(root)){
    SgType* base = rebuildBaseType(pointerType->get_base_type(), newBaseType);
    SgPointerType* newPointer = SageBuilder::buildPointerType(base);
    return newPointer;
  }
  //handle typedef, does not build new typedef. builds type around structure defined in typedef
  else if(SgTypedefType* defType = isSgTypedefType(root)){
    return rebuildBaseType(defType->get_base_type(), newBaseType);
  }
  //handle reference type
  else if(SgReferenceType* refType = isSgReferenceType(root)){
    SgType* base = rebuildBaseType(refType->get_base_type(), newBaseType);
    SgReferenceType* newReference = SageBuilder::buildReferenceType(base);
    return newReference;
  }
  //handle type modifiers(const, restrict, volatile)
  else if(SgModifierType* modType = isSgModifierType(root)){
    SgType* base =  rebuildBaseType(modType->get_base_type(), newBaseType);
    SgTypeModifier modifier = modType->get_typeModifier();
    SgModifierType* newMod;
    if(modifier.isRestrict()){
      newMod = SageBuilder::buildRestrictType(base);
    }
    else{
      SgConstVolatileModifier cmod = modifier.get_constVolatileModifier();
      if(cmod.isConst()){
        newMod = SageBuilder::buildConstType(base);
      }
      else if(cmod.isVolatile()){
        newMod = SageBuilder::buildVolatileType(base);
      }
      else{
        newMod = SageBuilder::buildModifierType(base);
      }
    }
    return newMod;
  }
  //reached base so return new base instead
  else{
    return newBaseType;
  }
}

//Changes the type of an a node. Likly came from resolving a handle
int TFTypeTransformer::changeHandleType(SgNode* handle, SgType* newType, bool base, bool listing){
  SgInitializedName* initName = isSgInitializedName(handle);
  if(SgVariableDeclaration* varDec = isSgVariableDeclaration(handle)){
    initName = SgNodeHelper::getInitializedNameOfVariableDeclaration(varDec);
  }
  if(initName != nullptr){
    SgType* changeType = newType;
    if(base){
      SgType* oldType = initName->get_type();
      changeType = rebuildBaseType(oldType, newType);
    }
    if(!listing){
      string varName = initName->get_name().getString();
      TFTypeTransformer::trace("Analysis: Found declaration of variable "+varName+".");// Change type to "+changeType->unparseToString());
      _transformer.addTransformation(varName, changeType, initName);
      if(_setFlag) return 1 + changeSet(handle, initName->get_type(), newType, base, listing);
      return 1;
    }
  } else if(SgFunctionDeclaration* funDecl = isSgFunctionDeclaration(handle)) {
      SgFunctionDefinition* funDef = funDecl->get_definition();
      if (funDef != NULL) {
        SgType* funRetType=SgNodeHelper::getFunctionReturnType(funDef);
        SgFunctionType* funType = funDecl->get_type();
        if(base){
          newType = rebuildBaseType(funRetType, newType);
        }
        string funName = funDecl->get_name();
        if(!listing){
          TFTypeTransformer::trace("Analysis: Found return "+((funName=="")? "" : "in "+funName)+".");// Change type to "+newType->unparseToString());
          _transformer.addTransformation(funName+":$return", newType, funDecl);
          if(_setFlag) return 1 + changeSet(handle, funRetType, newType, base, listing);
          return 1;
        }
      }
  }
  return 0;
}


//Given a node will change evey type that needs to be changed based upon type connections
int TFTypeTransformer::changeSet(SgNode* node, SgType* fromType, SgType* toType, bool base, bool listing){
  set<SgNode*>* nodeSet = getSet(node, fromType);
  bool tempFlag = changeSetFlag(false);  
  int changes = 0;
  if(nodeSet){
    if(nodeSet->size() > 1){
      TFTypeTransformer::trace("Analysis: Finding memebrs of set.");
      for(auto i = nodeSet->begin(); i != nodeSet->end(); ++i){
        if(*i != node) changes +=  changeHandleType(*i, toType, base, listing);
      }
      TFTypeTransformer::trace("Analysis: Finished set.");
    }
  }
  changeSetFlag(tempFlag);
  return changes;
}

//given an initialized name will change it's type to the new given type
int TFTypeTransformer::changeType(SgInitializedName* varInitName, SgType* newType, SgType* oldType, string varName, bool base, SgFunctionDeclaration* funDecl, SgNode* handleNode,bool listing){
  SgType* baseType;
  if(base){
    SgType* oldInitType = varInitName->get_type();
    baseType = rebuildBaseType(oldInitType, newType);
  }else{
    baseType = newType;
  }
  string scopeName = "global";
  if(funDecl){
    scopeName = funDecl->get_name();
    scopeName = "function:<" + scopeName + ">";
  }
  if(listing){
    addToActionList(varName, scopeName, oldType, newType, handleNode, base);
    return 0; 
  } 
  else{
    TFTypeTransformer::trace("Analysis: Found declaration of variable "+varName+" in "+scopeName+".");// Change type to "+baseType->unparseToString());
    _transformer.addTransformation(scopeName+":"+varName,baseType,varInitName);
    if(_setFlag == true) return 1 + changeSet(handleNode, varInitName->get_type(), newType, base, listing);
    return 1;
  }
}

int TFTypeTransformer::changeTypeIfInitNameMatches(SgInitializedName* varInitName, SgNode* root, string varNameToFind, SgType* newType, bool base, SgNode* handleNode,bool listing) {
  ROSE_ASSERT(varInitName != NULL);

  int foundVar=0;
  string varName = varInitName->get_name().getString();
  if(varName == varNameToFind) {
    foundVar += changeType(varInitName, newType, nullptr, varName, base, isSgFunctionDeclaration(root), handleNode, listing);
  }

  return foundVar;
}

//Will change the type of the variable if the from type matches else returns 0
int TFTypeTransformer::changeTypeIfFromTypeMatches(SgInitializedName* varInitName, SgNode* root, SgType* newType, SgType* fromType, bool base, SgNode* handleNode, bool listing){
  ROSE_ASSERT(varInitName != NULL);

  int foundVar = 0;
  SgType* oldType = varInitName->get_type();
  if(SgNodeHelper::isTypeBasedOn(oldType, fromType, base)){
    string varName = varInitName->get_name().getString();
    foundVar += changeType(varInitName, newType, fromType, varName, base, isSgFunctionDeclaration(root), handleNode, listing);
  }

  return foundVar;
}

//will search for variables to change. first by type if fromtype is provided then by name if it is not.
int TFTypeTransformer::changeVariableType(SgProject * project, SgFunctionDeclaration* funDecl, string varNameToFind, SgType* newType, bool base, SgType* fromType, bool listing) {

  ROSE_ASSERT(project != NULL);
  int foundVar=0;

  // Process type changes inside of a function
  if (funDecl != NULL) {
    if (SageInterface::insideSystemHeader(funDecl) || funDecl->get_name().getString().find("__builtin_") == 0) return 0;

    SgFunctionDefinition* funDef = funDecl->get_definition();

    if(funDef != NULL && varNameToFind != "TYPEFORGEret" && varNameToFind != "TYPEFORGEargs") {
      RoseAst ast(funDef);
      for(RoseAst::iterator i = ast.begin(); i != ast.end(); ++i) {
        if(SgVariableDeclaration* varDecl=isSgVariableDeclaration(*i)) {
          SgInitializedName * varInitName = SgNodeHelper::getInitializedNameOfVariableDeclaration(varDecl);
          if ( fromType != nullptr && varNameToFind == "TYPEFORGEbody" ) {
            foundVar += changeTypeIfFromTypeMatches(varInitName, funDecl, newType, fromType, base, varDecl, listing);
          } else if ( varNameToFind != "" && fromType == nullptr ) {
            foundVar += changeTypeIfInitNameMatches(varInitName, funDecl, varNameToFind, newType, base, varDecl, listing);
          }
        }
      }
    }

    std::vector<SgFunctionCallExp *> calls;
    if (funDecl->get_firstNondefiningDeclaration() == funDecl) {
      // TODO get all func call expr with function reference whose symbol point to funDecl
      RoseAst ast(project);
    }

    int param_idx = 0;
    for(auto varInitName : funDecl->get_parameterList()->get_args()) {
      int foundParam = 0;

      if (fromType != nullptr && varNameToFind == "TYPEFORGEargs") {
        foundParam += changeTypeIfFromTypeMatches(varInitName, funDecl, newType, fromType, base, varInitName, listing);
      } else if (varNameToFind != "" && fromType == nullptr) {
        foundParam += changeTypeIfInitNameMatches(varInitName, funDecl, varNameToFind, newType, base, varInitName, listing);
      }

      if (foundParam > 0) {
        for (auto call: calls) {
          SgCastExp * cast = isSgCastExp(call->get_args()->get_expressions()[param_idx]);
          if (cast != NULL && cast->isCompilerGenerated()) {
            _transformer.addTransformation("call-argument",NULL,cast);
          }
        }
      }

      foundVar += foundParam;
      param_idx++;
    }

    //Change return type
    if(fromType != nullptr && varNameToFind == "TYPEFORGEret") {
      SgFunctionType* funType = funDecl->get_type();
      SgType* funRetType = funType->get_return_type();
      if(SgNodeHelper::isTypeBasedOn(funRetType, fromType, base)){
        SgType* replaceType = newType;
        if(base){
          replaceType = rebuildBaseType(funRetType, newType);
        }
        string funName = SgNodeHelper::getFunctionName(funDecl);
        if(listing) {
          addToActionList("$return", funName, fromType, replaceType, funDecl, base);
        } else {
          TFTypeTransformer::trace("Analysis: Found return type "+((funName=="")? "" : "in "+funName)+".");
          int cnt=_transformer.addTransformation(funName+":$return",replaceType,funDecl);
          if(cnt==0) {
            cerr<<"Error: attempted to apply multiple changes to return type of function "<<funName<<endl;
            exit(1);
          }
          if(_setFlag)
            foundVar += changeSet(funDecl, fromType, replaceType, base, listing);
          foundVar++;
        }
      }
    }
  } else {
    // Change type of global variables
    ROSE_ASSERT(varNameToFind != "TYPEFORGEret" && varNameToFind != "TYPEFORGEargs");

    list<SgVariableDeclaration*> listOfGlobalVars = SgNodeHelper::listOfGlobalVars(project);
    for (auto varDecl: listOfGlobalVars) {
      SgInitializedName * varInitName = SgNodeHelper::getInitializedNameOfVariableDeclaration(varDecl);
      if (fromType != nullptr) {
        foundVar += changeTypeIfFromTypeMatches(varInitName, project, newType, fromType, base, varDecl, listing);
      } else {
        foundVar += changeTypeIfInitNameMatches(varInitName, project, varNameToFind, newType, base, varDecl, listing);
      }
    }
  }

  return foundVar;
}

void TFTypeTransformer::makeAllCastsExplicit(SgProject* root) {
  RoseAst ast(root);
  for(RoseAst::iterator i=ast.begin();i!=ast.end();++i) {
    if(SgCastExp* castExp=isSgCastExp(*i)) {
      if(castExp->isCompilerGenerated()) {
	castExp->unsetCompilerGenerated();
      }
    }
  }
}

void TFTypeTransformer::annotateImplicitCastsAsComments(SgProject* root) {
  RoseAst ast(root);
  string matchexpression="$CastNode=SgCastExp($CastOpChild)";
  AstMatching m;
  MatchResult r=m.performMatching(matchexpression,root);
  //cout << "Number of matched patterns with bound variables: " << r.size() << endl;
  list<string> report;
  int statementTransformations=0;
  for(MatchResult::reverse_iterator i=r.rbegin();i!=r.rend();++i) {
    statementTransformations++;
    SgCastExp* castExp=isSgCastExp((*i)["$CastNode"]);
    ROSE_ASSERT(castExp);
    SgExpression* childNode=isSgExpression((*i)["$CastOpChild"]);
    ROSE_ASSERT(childNode);
    if(castExp->isCompilerGenerated()) {
      SgType* castType=castExp->get_type();
      string castTypeString=castType->unparseToString();
      SgType* castedType=childNode->get_type();
      string castedTypeString=castedType->unparseToString();
      string reportLine="compiler generated cast: "
        +SgNodeHelper::sourceLineColumnToString(castExp->get_parent())
        +": "+castTypeString+" <== "+castedTypeString;
      if(castType==castedType) {
        reportLine+=" [ no change in type. ]";
      }
      // line are created in reverse order
      report.push_front(reportLine); 
      
      string newSourceCode;
      newSourceCode="/*CAST("+castTypeString+")*/";
      newSourceCode+=castExp->unparseToString();
      castExp->unsetCompilerGenerated(); // otherwise it is not replaced
      SgNodeHelper::replaceAstWithString(castExp,newSourceCode);
    }
  }
  for(list<string>::iterator i=report.begin();i!=report.end();++i) {
    cout<<*i<<endl;
  }
  //m.printMarkedLocations();
  //m.printMatchOperationsSequence();
  cout<<"Number of compiler generated casts: "<<statementTransformations<<endl;
}

void TFTypeTransformer::setTraceFlag(bool traceFlag) {
  _traceFlag=traceFlag;
}

bool TFTypeTransformer::getTraceFlag() {
  return _traceFlag;
}

void TFTypeTransformer::trace(string s) {
  if(TFTypeTransformer::_traceFlag) {
    cout<<"TRACE: "<<s<<endl;
  }
}

int TFTypeTransformer::getTotalNumChanges() {
  return _totalNumChanges;
}

void TFTypeTransformer::generateCsvTransformationStats(string fileName,int numTypeReplace,TFTypeTransformer& tt, TFTransformation& tfTransformation) {
  stringstream ss;
  ss<<numTypeReplace
    <<","<<tt.getTotalNumChanges()
    <<","<<tfTransformation.readTransformations
    <<","<<tfTransformation.writeTransformations
    <<","<<tfTransformation.arrayOfStructsTransformations
    <<","<<tfTransformation.adIntermediateTransformations
    <<endl;
  CppStdUtilities::writeFile(fileName,ss.str());
}

void TFTypeTransformer::printTransformationStats(int numTypeReplace,TFTypeTransformer& tt, TFTransformation& tfTransformation) {
  stringstream ss;
  int numReplacementsFound=tt.getTotalNumChanges();
  int arrayReadAccesses=tfTransformation.readTransformations;
  int arrayWriteAccesses=tfTransformation.writeTransformations;
  int arrayOfStructsAccesses=tfTransformation.arrayOfStructsTransformations;
  int adIntermediateTransformations=tfTransformation.adIntermediateTransformations;
  cout<<"STATS: number of variable type replacements: "<<numReplacementsFound<<endl;
  cout<<"STATS: number of transformed array read accesses: "<<arrayReadAccesses<<endl;
  cout<<"STATS: number of transformed array write accesses: "<<arrayWriteAccesses<<endl;
  cout<<"STATS: number of transformed arrays of structs accesses: "<<arrayOfStructsAccesses<<endl;
  cout<<"STATS: number of ad_intermediate transformations: "<<adIntermediateTransformations<<endl;
  int totalTransformations=numReplacementsFound+arrayReadAccesses+arrayWriteAccesses+arrayOfStructsAccesses+adIntermediateTransformations;
  cout<<"STATS: total number of transformations: "<<totalTransformations<<endl;
}

}

