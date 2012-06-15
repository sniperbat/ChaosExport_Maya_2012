#include <maya/MString.h>
#include <maya/MFnPlugin.h>
#include <maya/MGlobal.h>
#include <maya/MSelectionList.h>
#include <maya/MItSelectionList.h>
#include <maya/MDagPath.h>
#include <maya/MItDag.h>
#include <maya/MFnDagNode.h>
#include <maya/MFnMesh.h>
#include <maya/MVector.h>
#include <maya/MPoint.h>
#include <maya/MIntArray.h>
#include <maya/MFloatArray.h>
#include <maya/MFnDagNode.h>
#include <maya/MFnSet.h>
#include <maya/MPlugArray.h>
#include <maya/MMatrix.h>
#include <maya/MAnimUtil.h>
#include <maya/MItDependencyGraph.h>
#include <maya/MFnAnimCurve.h>
#include <maya/MDistance.h>

#include <vector>
#include <boost/any.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/assign.hpp>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
using namespace boost::assign;
#include <limits.h>

#include "ChaosExport.h"
#include "tinyxml2.h"

//--------------------------------------------------------------------------------------------------
static tinyxml2::XMLDocument xmlFile;
static tinyxml2::XMLElement * modelElement = NULL;
static MString extension = "chsmodel";
static MString magicHeader = "chmo";

struct ChsMesh{
  bool isShort;
  bool hasVertexColor;
  bool hasUV;
  bool hasTexture;
  std::vector<float> vertexArray;
  std::vector<unsigned short> usIndexArray;
  std::vector<unsigned int> uiIndexArray;
  float transform[4][4];
  
  void addPosition( const MPoint & pos ){
    this->vertexArray += pos.x, pos.y, pos.z;
  }
  
  void addNormal( const MVector & normal ){
    this->vertexArray += normal.x, normal.y, normal.z;    
  }
  
  void addUV( float u, float v){
    this->vertexArray += u, v; 
  }
  
  void addColor( const MColor & color ){
    this->vertexArray += color.r, color.g, color.b, color.a;
  }
  
  void addIndexValue( int indexValue ){
    if( isShort ){
      this->usIndexArray += indexValue;
    }
    else {
      this->uiIndexArray += indexValue;
    }
  }
  
};

static std::vector< boost::shared_ptr<ChsMesh> > meshList;

enum Format{
  UNKNOWN_FORMAT = -1,
  XML_FORMAT,
  BINARY_FORMAT,
}format;

//--------------------------------------------------------------------------------------------------
enum{
  POSITION,
  NORMAL,
  TEXCOORD0,
  COLOR,
};

struct Attribute{
  MString id;
  int stride;
  MString type;
};

static Attribute attributes[]={
  { "position",    3, "GL_FLOAT" },
  { "normal",      3, "GL_FLOAT" },
  { "texcoord0",   2, "GL_FLOAT" },
  { "vertexColor", 4, "GL_FLOAT" },
};


//--------------------------------------------------------------------------------------------------
enum ChsAnimCurveName{
  CHS_ANIMCURVE_VISIBILITY,
  CHS_ANIMCURVE_SX,
  CHS_ANIMCURVE_SY,
  CHS_ANIMCURVE_SZ,
  CHS_ANIMCURVE_RX,
  CHS_ANIMCURVE_RY,
  CHS_ANIMCURVE_RZ,
  CHS_ANIMCURVE_TX,
  CHS_ANIMCURVE_TY,
  CHS_ANIMCURVE_TZ,
  CHS_ANIMCURVE_MAX,
  CHS_ANIMCURVE_INVALID = -1,
};

//--------------------------------------------------------------------------------------------------
static const MString animCurveNames[CHS_ANIMCURVE_MAX] = {
  "visibility",
  "scaleX",
  "scaleY",
  "scaleZ",
  "rotationX",
  "rotationY",
  "rotationZ",
  "translationX",
  "translationY",
  "translationZ",
};

//--------------------------------------------------------------------------------------------------
struct AnimCurve{
  float time;
  int type;
  float value;
};

std::vector<AnimCurve> animCurveList[CHS_ANIMCURVE_MAX];

//--------------------------------------------------------------------------------------------------
template<typename T> void writeValueToFile( std::ofstream & ofs, T * value, int count ){
  ofs.write( (const char * )value, sizeof(T) * count );
}

//--------------------------------------------------------------------------------------------------
MStatus checkExportSelection( MPxFileTranslator::FileAccessMode mode, bool & isExportSelection ){
  if( MPxFileTranslator::kExportAccessMode == mode ){
    isExportSelection = false;
  }
  else if( MPxFileTranslator::kExportActiveAccessMode == mode ){
    isExportSelection = true;
  }
  else{
    return MStatus::kFailure;
  }
  return MStatus::kSuccess;
}

//--------------------------------------------------------------------------------------------------
void initXMLFile( void ){
  xmlFile.DeleteChildren();
  modelElement = xmlFile.NewElement( "ChsModel" );
  xmlFile.InsertEndChild( modelElement );
}

//--------------------------------------------------------------------------------------------------
void writeBinaryPartToFile( ofstream & newFile ){
  int meshCount = meshList.size();
  //write vertex and index data
  for( int meshIdx = 0; meshIdx < meshCount; meshIdx++ ){
    boost::shared_ptr<ChsMesh> & mesh = meshList[meshIdx];
    int countOfVertex = mesh->vertexArray.size();
    int sizeOfVertex =  countOfVertex * sizeof( float );
    writeValueToFile( newFile, &sizeOfVertex, 1 );
    writeValueToFile( newFile, mesh->vertexArray.data(), countOfVertex );
    if( mesh->isShort ){
      int countOfIndex = mesh->usIndexArray.size();
      int sizeOfIndex = countOfIndex * sizeof( unsigned short );
      writeValueToFile( newFile, &sizeOfIndex, 1 );
      writeValueToFile( newFile, mesh->usIndexArray.data(), countOfIndex );
    }
    else{
      int countOfIndex = mesh->uiIndexArray.size();
      int sizeOfIndex = countOfIndex * sizeof( unsigned int );
      writeValueToFile( newFile, &sizeOfIndex, 1 );
      writeValueToFile( newFile, mesh->uiIndexArray.data(), countOfIndex );
    }
  }
}

//--------------------------------------------------------------------------------------------------
void writeXMLPartToFile( ofstream & newFile ){
  tinyxml2::XMLPrinter printer( NULL, true );
  xmlFile.Print( &printer );
  int xmlFileSize = printer.CStrSize();
  if( BINARY_FORMAT == format ){
    xmlFileSize = ( xmlFileSize + 3 ) / 4 * 4;//address align
    writeValueToFile( newFile, &xmlFileSize,1);
  }
  boost::scoped_ptr<char> xmlBuffer( new char[xmlFileSize] );
  memcpy( xmlBuffer.get(), printer.CStr(), xmlFileSize );
  writeValueToFile( newFile, xmlBuffer.get(), xmlFileSize );
}

//--------------------------------------------------------------------------------------------------
MStatus writeToFile( const MString & fullFileName ){
  ofstream newFile( fullFileName.asChar(), ios::out );
  if( !newFile ){
    MGlobal::displayError( fullFileName + ": could not be opened for reading" );
    return MStatus::kFailure;
  }
  //enable automatic flushing of the output stream after any output operation
  newFile.setf( ios::unitbuf );
  if( BINARY_FORMAT == format ){
    writeValueToFile( newFile, magicHeader.asChar(),magicHeader.length() );
  }
  writeXMLPartToFile( newFile );
  if( BINARY_FORMAT == format ){
    writeBinaryPartToFile( newFile );
  }
  newFile.flush();
  newFile.close();
  return MStatus::kSuccess;
}

//--------------------------------------------------------------------------------------------------
void makeAttributeElement( int type, tinyxml2::XMLElement * meshElement ){
  tinyxml2::XMLElement * attributeElement = xmlFile.NewElement( "ChsAttribute" );
  attributeElement->SetAttribute( "id", attributes[type].id.asChar() );
  attributeElement->SetAttribute( "stride", attributes[type].stride );
  attributeElement->SetAttribute( "type", attributes[type].type.asChar() );
  meshElement->InsertEndChild( attributeElement );
}

//------------------------------------------------------------------------------------------------
enum ChsShaderUniformDataType {
  CHS_SHADER_UNIFORM_1_FLOAT,
  CHS_SHADER_UNIFORM_1_INT,
  CHS_SHADER_UNIFORM_VEC2_FLOAT,
  CHS_SHADER_UNIFORM_VEC2_INT,
  CHS_SHADER_UNIFORM_VEC3_FLOAT,
  CHS_SHADER_UNIFORM_VEC3_INT,
  CHS_SHADER_UNIFORM_VEC4_FLOAT,
  CHS_SHADER_UNIFORM_VEC4_INT,
  CHS_SHADER_UNIFORM_MAT2,
  CHS_SHADER_UNIFORM_MAT3,
  CHS_SHADER_UNIFORM_MAT4,
};

//--------------------------------------------------------------------------------------------------
template <typename T> void makePropertyElement( const MString & name , ChsShaderUniformDataType type,
                                               unsigned int count, T value, tinyxml2::XMLElement * materialElement ){
  tinyxml2::XMLElement * propertyElement = xmlFile.NewElement( "ChsProperty" );
  propertyElement->SetAttribute( "name", name.asChar() );
  propertyElement->SetAttribute( "type", type );
  propertyElement->SetAttribute( "count", count );
  propertyElement->SetAttribute( "value", value );
  materialElement->InsertEndChild( propertyElement );
}

//--------------------------------------------------------------------------------------------------
template <typename T> void makePropertyElement( const MString & name , ChsShaderUniformDataType type,
                                               unsigned int count, std::vector<T> valueArray,
                                               tinyxml2::XMLElement * materialElement ){
  std::string valueStr;
  BOOST_FOREACH( T & value , valueArray )
  valueStr.append( boost::lexical_cast<std::string>( value ) ).append( " " );
  makePropertyElement( name, type, count, valueStr.c_str(), materialElement );
}

//--------------------------------------------------------------------------------------------------
enum{
  DIFFUSE_COLOR,
};

//--------------------------------------------------------------------------------------------------
struct MaterialChannel{
  const MString channelName;
  const MString uniformName;
  std::string textureFileName;
  const int activeUnit;
  MaterialChannel( const MString & cName, const MString & uName, int au ):
    channelName( cName ), uniformName( uName ), activeUnit( au ){
      r=g=b=1.0;
  }
  double r, g, b;
};

MaterialChannel materialChannels[]={
  MaterialChannel( "color", "diffuse", 0 ),
  MaterialChannel( "ambientColor", "ambient", 1 ),
};

//--------------------------------------------------------------------------------------------------
void getMaterialAttributeAtChannel( int channelIndex, MFnDependencyNode fnMaterial ){
  MPlug channelPlug;
  MPlugArray plugs;
  MaterialChannel & materialChannel = materialChannels[channelIndex];
  channelPlug = fnMaterial.findPlug( materialChannel.channelName );
  channelPlug.connectedTo( plugs, true,false );
  materialChannel.textureFileName.clear();
  if( plugs.length() > 0 ){
    MObject obj = plugs[0].node();
		if( obj.apiType() == MFn::kFileTexture ){
      MFnDependencyNode fnFile( obj );
      MPlug ftnPlug = fnFile.findPlug("fileTextureName");
      MString texFilenameStr;
      ftnPlug.getValue( texFilenameStr );
      std::string textureFileName = texFilenameStr.asChar();
      int found = textureFileName.find_last_of("/");
      materialChannel.textureFileName = textureFileName.substr( found+1 );
    }
  }
  else {
    //just output colors
    channelPlug.child( 0 ).getValue( materialChannel.r );
    channelPlug.child( 1 ).getValue( materialChannel.g );
    channelPlug.child( 2 ).getValue( materialChannel.b );
  }
}

//--------------------------------------------------------------------------------------------------
void makeMaterialAttribute( int channelIndex, boost::shared_ptr<ChsMesh> & mesh, tinyxml2::XMLElement * materialElement ){
  const MaterialChannel & materialChannel = materialChannels[channelIndex];
  if( !materialChannel.textureFileName.empty() ){
    tinyxml2::XMLElement * textureElement = xmlFile.NewElement( "ChsTexture2D" );
    textureElement->SetAttribute( "src", materialChannel.textureFileName.c_str() );
    MString sampleName = materialChannel.uniformName + "Texture";
    textureElement->SetAttribute( "sampleName", sampleName.asChar() );
    textureElement->SetAttribute( "activeUnit", materialChannel.activeUnit );
    materialElement->InsertEndChild( textureElement );
  }
  else{
    MString colorName = materialChannel.uniformName + "Color";
    std::vector<float> rgb;
    rgb += materialChannel.r, materialChannel.g, materialChannel.b, 1.0;
    makePropertyElement( colorName, CHS_SHADER_UNIFORM_VEC4_FLOAT, 1, rgb, materialElement );
  }
}

//--------------------------------------------------------------------------------------------------
void makeMaterialElement( const MFnMesh & fnMesh, boost::shared_ptr<ChsMesh> & mesh, tinyxml2::XMLElement * meshElement ){
  tinyxml2::XMLElement * materialElement = xmlFile.NewElement( "ChsMaterial" );
  meshElement->InsertEndChild( materialElement );
  tinyxml2::XMLElement * shaderElement = xmlFile.NewElement( "ChsVertexShader" );
  shaderElement->SetAttribute( "src", "Shader.vsh" );
  materialElement->InsertEndChild( shaderElement );
  shaderElement = xmlFile.NewElement( "ChsFragmentShader" );
  shaderElement->SetAttribute( "src", "Shader.fsh" );
  materialElement->InsertEndChild( shaderElement );
  makePropertyElement( "hasVertexColor", CHS_SHADER_UNIFORM_1_INT, 1, mesh->hasVertexColor, materialElement );
  makePropertyElement( "hasTexture", CHS_SHADER_UNIFORM_1_INT, 1, mesh->hasTexture, materialElement );
  makeMaterialAttribute( DIFFUSE_COLOR, mesh, materialElement );
}

//--------------------------------------------------------------------------------------------------
void makeIndexBufferElement( boost::shared_ptr<ChsMesh> & mesh, tinyxml2::XMLElement * meshElement ){
  tinyxml2::XMLElement * indexElement = xmlFile.NewElement( "ChsIndexBuffer" );
  indexElement->SetAttribute( "isShort" , mesh->isShort );
  int count = mesh->isShort ? mesh->usIndexArray.size() : mesh->uiIndexArray.size();
  indexElement->SetAttribute( "count" , count );
  if( XML_FORMAT == format ){
    std::string textStr;
    if( mesh->isShort ){
      BOOST_FOREACH( unsigned short & value , mesh->usIndexArray ){
        textStr.append( boost::lexical_cast<std::string>( value ) ).append( " " );
      }
      tinyxml2::XMLText * text = xmlFile.NewText( textStr.c_str() );
      indexElement->InsertEndChild( text );
    }
    else{
      BOOST_FOREACH( unsigned int & value , mesh->uiIndexArray ){
        textStr.append( boost::lexical_cast<std::string>( value ) ).append( " " );
      }
      tinyxml2::XMLText * text = xmlFile.NewText( textStr.c_str() );
      indexElement->InsertEndChild( text );
    }
  }
  meshElement->InsertEndChild( indexElement );  
}

//--------------------------------------------------------------------------------------------------
void makeVertexBufferElement( boost::shared_ptr<ChsMesh> & mesh, tinyxml2::XMLElement * meshElement ){
  tinyxml2::XMLElement * vertexElement = xmlFile.NewElement( "ChsVertexBuffer" );
  int count = mesh->vertexArray.size();
  vertexElement->SetAttribute( "count" , count );
  if( XML_FORMAT == format ){
    std::string textStr;
    BOOST_FOREACH( float & value , mesh->vertexArray ){
      textStr.append( boost::lexical_cast<std::string>( value ) ).append( " " );
    }
    tinyxml2::XMLText * text = xmlFile.NewText( textStr.c_str() );
    vertexElement->InsertEndChild( text );
  }
  meshElement->InsertEndChild( vertexElement );
}

//--------------------------------------------------------------------------------------------------
void makeTransformElement( boost::shared_ptr<ChsMesh> & mesh, tinyxml2::XMLElement * meshElement ){
  tinyxml2::XMLElement * transformElement = xmlFile.NewElement( "ChsMatrix" );
  transformElement->SetAttribute( "id", "transform" );
  std::string textStr;
  for( int i=0;i<4;i++){
    for( int j=0;j<4;j++){
      textStr.append( boost::lexical_cast<std::string>( mesh->transform[i][j] ) ).append( " " );
    }
  }
  tinyxml2::XMLText * valueText = xmlFile.NewText( textStr.c_str() );
  transformElement->InsertEndChild( valueText );
  meshElement->InsertEndChild( transformElement );
}

//--------------------------------------------------------------------------------------------------
void makeAnimCurveElement( boost::shared_ptr<ChsMesh> & mesh, tinyxml2::XMLElement * meshElement ){
  tinyxml2::XMLElement * animCurveSetElement = xmlFile.NewElement( "ChsAnimCurveSet" );
  for(int i = 0; i < CHS_ANIMCURVE_MAX; i++ ){
    std::vector<AnimCurve> & animCurves = animCurveList[i];
    int size = animCurves.size();
    if( size > 0 ){
      tinyxml2::XMLElement * animCurveElement = xmlFile.NewElement( "ChsAnimCurve" );
      animCurveElement->SetAttribute( "name", animCurveNames[i].asChar() );
      animCurveElement->SetAttribute( "count", size );
      std::string textStr;
      for( int curveUnitCount = 0; curveUnitCount < size; curveUnitCount++ ){
        const AnimCurve & animCurve = animCurves[curveUnitCount];
        textStr.append( boost::lexical_cast<std::string>( animCurve.time ) ).append( " " );
        textStr.append( boost::lexical_cast<std::string>( animCurve.type ) ).append( " " );
        textStr.append( boost::lexical_cast<std::string>( animCurve.value ) ).append( " " );
      }
      tinyxml2::XMLText * valueText = xmlFile.NewText( textStr.c_str() );
      animCurveElement->InsertEndChild( valueText );
      animCurveSetElement->InsertEndChild( animCurveElement );
    }
  }
  meshElement->InsertEndChild( animCurveSetElement );
}
//--------------------------------------------------------------------------------------------------
void makeXMLPart( const MFnMesh & fnMesh, boost::shared_ptr<ChsMesh> & mesh, tinyxml2::XMLElement * modelElement ){
  tinyxml2::XMLElement * meshElement = xmlFile.NewElement( "ChsMesh" );
  MString meshId = fnMesh.name();
  meshElement->SetAttribute( "id", meshId.asChar() );
  makeAttributeElement( POSITION, meshElement );
  makeAttributeElement( NORMAL, meshElement );
  if( mesh->hasUV && mesh->hasTexture ){
    makeAttributeElement( TEXCOORD0, meshElement );
  }
  if( mesh->hasVertexColor ){
    makeAttributeElement( COLOR, meshElement );
  }
  makeVertexBufferElement( mesh, meshElement );
  makeIndexBufferElement( mesh, meshElement );
  makeTransformElement( mesh, meshElement );
  if( mesh->isAnimated ){
    makeAnimCurveElement( mesh, meshElement );
  }
  makeMaterialElement( fnMesh, mesh, meshElement );
  modelElement->InsertEndChild( meshElement );
}

//--------------------------------------------------------------------------------------------------
struct VertexUnit{
  int vertexId;
  int uvId;
};

std::vector< VertexUnit > vertexList;
//--------------------------------------------------------------------------------------------------
void getIndexData( const MFnMesh & fnMesh, boost::shared_ptr<ChsMesh> & mesh ){
  int numPolygons = fnMesh.numPolygons();
  mesh->isShort = ( numPolygons * 3 < USHRT_MAX );
  vertexList.clear();
  
  for( int polygonId = 0; polygonId < numPolygons; polygonId++ ){
    MIntArray vertexListOfPolygon;
    fnMesh.getPolygonVertices( polygonId, vertexListOfPolygon );
    int vertexCountOfPolygon = vertexListOfPolygon.length();
    for( int vertexIndex = 0; vertexIndex < vertexCountOfPolygon; vertexIndex++ ){
      int vertexId = vertexListOfPolygon[vertexIndex];
      int uvId;
      fnMesh.getPolygonUVid( polygonId, vertexIndex, uvId );

      int index = -1;
      int indexCount = 0;
      BOOST_FOREACH( VertexUnit & unit, vertexList ){
        if( unit.uvId == uvId && unit.vertexId == vertexId ){
          index = indexCount;
          break;
        }
        indexCount++;
      }
      if( index == -1 ){
        index = vertexList.size();
        VertexUnit unit = { vertexId, uvId };
        vertexList += unit;
      }
      mesh->addIndexValue( index );
    }
  }
}

//--------------------------------------------------------------------------------------------------
void getVertexData( MFnMesh & fnMesh, boost::shared_ptr<ChsMesh> & mesh ){
  //check uv
  mesh->hasUV = fnMesh.numUVs() > 0;
  MFloatArray uArray, vArray;
  if( mesh->hasUV && mesh->hasTexture ){
    fnMesh.getUVs( uArray, vArray );
  }
  //check vertex color
  mesh->hasVertexColor = fnMesh.numColors() > 0;
  MColorArray colors;
  if( mesh->hasVertexColor ){
    fnMesh.getVertexColors( colors );
  }
  BOOST_FOREACH( VertexUnit & unit, vertexList ){
    int vertexId = unit.vertexId;
    int uvId = unit.uvId;
    MPoint pos;
    MVector normal;
    fnMesh.getPoint( vertexId, pos, MSpace::kObject );
    pos.cartesianize();
    mesh->addPosition( pos );
    fnMesh.getVertexNormal( vertexId, true, normal, MSpace::kObject );
    mesh->addNormal( normal );
    if( mesh->hasUV && mesh->hasTexture ){
      mesh->addUV( uArray[uvId], vArray[uvId] );
    }
    if( mesh->hasVertexColor ){
      mesh->addColor( colors[vertexId] );
    }
  }
}

//--------------------------------------------------------------------------------------------------
void makeBinaryPart( MFnMesh & fnMesh, boost::shared_ptr<ChsMesh> & mesh ){
  getIndexData( fnMesh, mesh );
  getVertexData( fnMesh, mesh );
}

//--------------------------------------------------------------------------------------------------
void processMaterial( MFnMesh & fnMesh, boost::shared_ptr<ChsMesh> & mesh ){
  MObjectArray shaders;
  MIntArray faceIndices;
  fnMesh.getConnectedShaders( 0, shaders, faceIndices );
  MFnDependencyNode fnShader( shaders[0]);
  MPlug surfaceShader = fnShader.findPlug("surfaceShader");
  MPlugArray materials;
  surfaceShader.connectedTo( materials, true, true);
  MObject materialNode = materials[0].node();
  getMaterialAttributeAtChannel( DIFFUSE_COLOR, materialNode );
  mesh->hasTexture = materialChannels[DIFFUSE_COLOR].textureFileName.empty() ? false : true;
}

//--------------------------------------------------------------------------------------------------
void processMeshTransform( MDagPath & dagPath, boost::shared_ptr<ChsMesh> & mesh ){
  MMatrix transform = dagPath.inclusiveMatrix();
  transform.get( mesh->transform );
}

//--------------------------------------------------------------------------------------------------
ChsAnimCurveName convertCurveName( MString strName ){
  MStringArray strArray;
  strName.split( '_', strArray );
  strName = strArray[strArray.length()-1];
  for( int i = 0; i < CHS_ANIMCURVE_MAX; i++ ){
    if( strName == animCurveNames[i] ){
      return (ChsAnimCurveName)i;
    }
  }
  return CHS_ANIMCURVE_INVALID;
}

//--------------------------------------------------------------------------------------------------
double getConversionByCurveType( const MFnAnimCurve::AnimCurveType & type ){
  double conversion = 1.0;
  switch( type ){
    case MFnAnimCurve::kAnimCurveTT:
    case MFnAnimCurve::kAnimCurveUT:
    case MFnAnimCurve::kAnimCurveUnknown:
      break;
    case MFnAnimCurve::kAnimCurveTA:
    case MFnAnimCurve::kAnimCurveUA:{
      MAngle angle(1.0);
      conversion = angle.as( MAngle::uiUnit() );
      break;
    }
    case MFnAnimCurve::kAnimCurveTL:
    case MFnAnimCurve::kAnimCurveUL:{
      MDistance distance(1.0);
      conversion = distance.as( MDistance::uiUnit() );
      break;
    }
    default:
      break;
  }
  return conversion;
}

//--------------------------------------------------------------------------------------------------
void processAnimCurve( MDagPath & dagPath ){
  bool isAnimated = MAnimUtil::isAnimated( dagPath );
  if( isAnimated ){
    MGlobal::displayInfo( "animation" );
    for( int i = 0; i< CHS_ANIMCURVE_MAX; i++ ){
      animCurveList[i].clear();
    }
    MObject dagPathNode = dagPath.node();
    MStatus status;
    MItDependencyGraph animIter( dagPathNode,
                                MFn::kAnimCurve,
                                MItDependencyGraph::kUpstream,
                                MItDependencyGraph::kDepthFirst,
                                MItDependencyGraph::kNodeLevel,
                                &status );
    if( status ){
      for ( animIter.reset(); !animIter.isDone(); animIter.next() ) {
        MObject anim = animIter.thisNode( &status );
        MFnAnimCurve animFn( anim, &status );
        ChsAnimCurveName curveName = convertCurveName( animFn.name() );
        if( curveName <= CHS_ANIMCURVE_INVALID || curveName >= CHS_ANIMCURVE_MAX )
          continue;//unknown animation curve name
        int numKeys = animFn.numKeys();
        MFnAnimCurve::AnimCurveType type = animFn.animCurveType();
        double conversion = getConversionByCurveType( type );
        for ( int key = 0; key < numKeys; key++ ){
          double time = animFn.time( key ).as( MTime::kSeconds );
          double value = conversion * animFn.value( key );
          AnimCurve curveUnit = { time, 0, value };
          animCurveList[curveName] += curveUnit;
        }
      }//for (; !animIter.isDone(); animIter.next()) 
    }//if( status )
  }//if( isAnimated )

}

//--------------------------------------------------------------------------------------------------
void processMesh( MDagPath & dagPath ){
  MStatus status;
  if( dagPath.hasFn( MFn::kMesh ) && (dagPath.childCount() == 0) ){
    MFnMesh fnMesh( dagPath, &status );
    if( !fnMesh.isIntermediateObject() ){
      boost::shared_ptr<ChsMesh> mesh( new ChsMesh );
      processMaterial( fnMesh, mesh );
      processMeshTransform( dagPath, mesh );
      
      makeBinaryPart( fnMesh, mesh );
      makeXMLPart( fnMesh, mesh, modelElement );
      
      meshList.push_back( mesh );
    }
  }
}

//--------------------------------------------------------------------------------------------------
MStatus processNode( MDagPath & dagPath ){
  processAnimCurve( dagPath );
  processMesh( dagPath );
  for (uint i=0; i<dagPath.childCount(); i++){
    MObject child = dagPath.child(i);
    MDagPath childPath = dagPath;
    childPath.push( child );
    processNode( childPath );
  }
  return MStatus::kSuccess;
}

//--------------------------------------------------------------------------------------------------
MStatus prepareXMLWithAll( void ){
  MGlobal::displayInfo("prepareXMLWithAll");
  MItDag dagIter;
  MFnDagNode worldDag( dagIter.root() );
  if( !worldDag.instanceCount( true ) ){
    MGlobal::displayInfo("nothing to export!");
    return MStatus::kFailure;
  }
  MDagPath worldPath;
  worldDag.getPath( worldPath );
  return processNode( worldPath );
}

//--------------------------------------------------------------------------------------------------
MStatus prepareXMLWithSelection( void ){
  MGlobal::displayInfo("prepareXMLWithSelection");
  MSelectionList activeSelectionList;
  MGlobal::getActiveSelectionList( activeSelectionList );
  MItSelectionList iter( activeSelectionList );
  MStatus status;
  for ( ; !iter.isDone(); iter.next()){								
    MDagPath dagPath;
    status = iter.getDagPath( dagPath );
    if( MStatus::kFailure == processNode( dagPath ) ){
      break;
    }
  }
  return status;
}

//--------------------------------------------------------------------------------------------------
MStatus ChaosExport::writer( const MFileObject &file,	const MString &/*options*/,	FileAccessMode mode ){
  meshList.clear();
  format = BINARY_FORMAT;
  
  bool isExportSelection;
  MStatus status;
  if( MStatus::kFailure == checkExportSelection( mode, isExportSelection ) )
    return MStatus::kFailure;
  
#if defined( OSMac_ )
  char nameBuffer[ MAXPATHLEN ];
  strcpy( nameBuffer, file.fullName().asChar() );
  const MString fullFileName( nameBuffer );
  strcpy( nameBuffer, file.name().asChar() );
  const MString shortFileName( nameBuffer );
#else
  const MString fullFileName = file.fullName();
  const MString shortFileName = file.name();
#endif
  
  initXMLFile();
  
  if( MStatus::kSuccess == (isExportSelection ? prepareXMLWithSelection() : prepareXMLWithAll()) ){
    MGlobal::displayInfo("writeToFile");
    modelElement->SetAttribute( "meshCount", static_cast<int>( meshList.size() ) );
    MString modelId = shortFileName.substring( 0, shortFileName.length()-extension.length()-2 );
    modelElement->SetAttribute( "id", modelId.asChar() );
    status = writeToFile( fullFileName );
  }

  if( MStatus::kSuccess == status ){
    MGlobal::displayInfo("Export to " + fullFileName + " successful!");
  }
  else{
    MGlobal::displayInfo("Failed export to " + fullFileName + " successful!");
  }
	return status;
}

//--------------------------------------------------------------------------------------------------
MPxFileTranslator::MFileKind ChaosExport::identifyFile( const MFileObject &file, const char * , short )const{
  MString name = file.name();
  int nameLength = name.length();
  int extensionLength = extension.length();
  if ( nameLength > extensionLength ){
    if( name.substring( nameLength - extensionLength, nameLength ) == extension ){
      return kIsMyFileType;
    }
  }
  return kNotMyFileType;
}

//--------------------------------------------------------------------------------------------------
MString ChaosExport::defaultExtension( void ) const{
  return extension;
}

//--------------------------------------------------------------------------------------------------
//	Plugin management
//--------------------------------------------------------------------------------------------------
MStatus initializePlugin( MObject obj ){
  MStatus status;
  MFnPlugin plugin( obj, "sniperbat", "1.0", "Any" );
  status = plugin.registerFileTranslator ( "chaosExport", const_cast<char*>( "none" ), ChaosExport::creator );
  if( !status ){
    status.perror( "registerFileTranslator" );
  }
  return status;
}

//--------------------------------------------------------------------------------------------------
MStatus uninitializePlugin( MObject obj ){
  MFnPlugin plugin( obj );
  MStatus status = plugin.deregisterFileTranslator( "chaosExport" );
  if( !status ){
    status.perror( "deregisterFileTranslator" );
  }
  return status;
}

//--------------------------------------------------------------------------------------------------
