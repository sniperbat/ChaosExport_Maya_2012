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
  int index;
};

static Attribute attributes[]={
  {    "position",    3,    "GL_FLOAT", 0 },
  {    "normal",      3,    "GL_FLOAT", 1  },
  {    "texcoord0",   2,    "GL_FLOAT", 2  },
  {    "vertexColor", 4,    "GL_FLOAT", 3  },
};

//--------------------------------------------------------------------------------------------------
template<typename T> void writeValueToFile( std::ofstream & ofs, T * value, int count ){
  ofs.write( (const char * )value, sizeof(T) * count );
}

//--------------------------------------------------------------------------------------------------
bool isVisible( MFnDagNode & fnDag, MStatus & status ){
  if( fnDag.isIntermediateObject() )
    return false;
  MPlug visPlug = fnDag.findPlug("visibility", &status);
  if( MStatus::kFailure == status ){
    MGlobal::displayError("MPlug::findPlug");
    return false;
  }
  else{
    bool visible;
    status = visPlug.getValue(visible);
    if( MStatus::kFailure == status )
      MGlobal::displayError("MPlug::getValue");
    return visible;
  }
}

//--------------------------------------------------------------------------------------------------
MStatus checkExportSelection( MPxFileTranslator::FileAccessMode mode, bool & isExportSelection ){
  if( MPxFileTranslator::kExportAccessMode == mode )
    isExportSelection = false;
  else if( MPxFileTranslator::kExportActiveAccessMode == mode )
    isExportSelection = true;
  else
    return MStatus::kFailure;
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
  tinyxml2::XMLPrinter printer( NULL, false );
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
  if( BINARY_FORMAT == format )
    writeValueToFile( newFile, magicHeader.asChar(),magicHeader.length() );
  writeXMLPartToFile( newFile );
  if( BINARY_FORMAT == format )
    writeBinaryPartToFile( newFile );
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
  attributeElement->SetAttribute( "index", attributes[type].index );
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
      BOOST_FOREACH( unsigned short & value , mesh->usIndexArray )
        textStr.append( boost::lexical_cast<std::string>( value ) ).append( " " );
      tinyxml2::XMLText * text = xmlFile.NewText( textStr.c_str() );
      indexElement->InsertEndChild( text );
    }
    else{
      BOOST_FOREACH( unsigned int & value , mesh->uiIndexArray )
        textStr.append( boost::lexical_cast<std::string>( value ) ).append( " " );
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
    BOOST_FOREACH( float & value , mesh->vertexArray )
      textStr.append( boost::lexical_cast<std::string>( value ) ).append( " " );
    tinyxml2::XMLText * text = xmlFile.NewText( textStr.c_str() );
    vertexElement->InsertEndChild( text );
  }
  meshElement->InsertEndChild( vertexElement );
}

//--------------------------------------------------------------------------------------------------
void makeXMLPart( const MFnMesh & fnMesh, boost::shared_ptr<ChsMesh> & mesh, tinyxml2::XMLElement * modelElement ){
  tinyxml2::XMLElement * meshElement = xmlFile.NewElement( "ChsMesh" );
  MString meshId = fnMesh.name();
  meshElement->SetAttribute( "id", meshId.asChar() );
  makeAttributeElement( POSITION, meshElement );
  makeAttributeElement( NORMAL, meshElement );
  if( mesh->hasUV && mesh->hasTexture )
    makeAttributeElement( TEXCOORD0, meshElement );
  if( mesh->hasVertexColor )
    makeAttributeElement( COLOR, meshElement );
  makeVertexBufferElement( mesh, meshElement );
  makeIndexBufferElement( mesh, meshElement );
  makeMaterialElement( fnMesh, mesh, meshElement );
  modelElement->InsertEndChild( meshElement );
}

//--------------------------------------------------------------------------------------------------
void getIndexData( const MFnMesh & fnMesh, boost::shared_ptr<ChsMesh> & mesh ){
  int numPolygons = fnMesh.numPolygons();
  mesh->isShort = ( numPolygons * 3 < USHRT_MAX );
  for( int polygonId = 0; polygonId < numPolygons; polygonId++ ){
    MIntArray vertexListOfPolygon;
    fnMesh.getPolygonVertices( polygonId, vertexListOfPolygon );
    int vertexCountOfPolygon = vertexListOfPolygon.length();
    for( int vertexIndex = 0; vertexIndex < vertexCountOfPolygon; vertexIndex++ ){
      int vertexId = vertexListOfPolygon[vertexIndex];
      if( mesh->isShort )
        mesh->usIndexArray += vertexId;
      else
        mesh->uiIndexArray += vertexId;
    }
  }
}

//--------------------------------------------------------------------------------------------------
void getVertexData( MFnMesh & fnMesh, boost::shared_ptr<ChsMesh> & mesh ){
  int numVertices = fnMesh.numVertices();
  mesh->hasVertexColor = fnMesh.numColors() > 0;
  MFloatArray uArray, vArray;
  if( ( mesh->hasUV = fnMesh.numUVs() )>0 && mesh->hasTexture )
    fnMesh.getUVs( uArray, vArray );
  MColorArray colors;
  fnMesh.getVertexColors( colors );
  for( int vertexId = 0; vertexId < numVertices; vertexId++ ){
    MPoint pos;
    MVector normal;
    fnMesh.getPoint( vertexId, pos, MSpace::kWorld );
    mesh->vertexArray += pos.x, pos.y,pos.z;
    fnMesh.getVertexNormal( vertexId,true,normal, MSpace::kWorld );
    mesh->vertexArray += normal.x, normal.y,normal.z;
    if( mesh->hasUV && mesh->hasTexture ){
      mesh->vertexArray += uArray[vertexId], vArray[vertexId];
      MString info = "u:";
      info += uArray[vertexId];
      info += "v:";
      info += vArray[vertexId];
      MGlobal::displayInfo( info );
    }
    if( mesh->hasVertexColor )
      mesh->vertexArray += colors[vertexId].r, colors[vertexId].g, colors[vertexId].b, colors[vertexId].a;
  }
}

//--------------------------------------------------------------------------------------------------
void makeBinaryPart( MFnMesh & fnMesh, boost::shared_ptr<ChsMesh> & mesh ){
  getVertexData( fnMesh, mesh );
  getIndexData( fnMesh, mesh );
}

//--------------------------------------------------------------------------------------------------
MStatus processMesh( MDagPath & dagPath ){
  MStatus status;
  MFnDagNode dagNode( dagPath, &status );
  if ( dagNode.isIntermediateObject() || 
      !dagPath.hasFn( MFn::kMesh ) ||
       dagPath.hasFn( MFn::kTransform ) )
    return MStatus::kFailure;
  
  MFnMesh fnMesh( dagPath, &status );
  if( MStatus::kFailure == status)
    return MStatus::kFailure;
  
  MObjectArray shaders;
  MIntArray faceIndices;
  fnMesh.getConnectedShaders( 0, shaders, faceIndices );
  MFnDependencyNode fnShader( shaders[0]);
  MPlug surfaceShader = fnShader.findPlug("surfaceShader");
  MPlugArray materials;
  surfaceShader.connectedTo( materials, true, true);
  MObject materialNode = materials[0].node();
  getMaterialAttributeAtChannel( DIFFUSE_COLOR, materialNode );

  boost::shared_ptr<ChsMesh> mesh( new ChsMesh );
  mesh->hasTexture = materialChannels[DIFFUSE_COLOR].textureFileName.empty() ? false : true;
  
  meshList.push_back( mesh );
  makeBinaryPart( fnMesh, mesh );
  makeXMLPart( fnMesh, mesh, modelElement );
  return MStatus::kSuccess;
}

//--------------------------------------------------------------------------------------------------
MStatus prepareXMLWithAll( void ){
  MGlobal::displayInfo("prepareXMLWithAll");
  MStatus status;
  MItDag itDag( MItDag::kDepthFirst, MFn::kMesh, &status );
  if( MStatus::kFailure == status ) {
    MGlobal::displayError("MItDag::MItDag");
    return MStatus::kFailure;
  }
  if( !itDag.instanceCount( true ) ){
    MGlobal::displayInfo("nothing to export!");
    return MStatus::kFailure;
  }
  while( !itDag.isDone() ){
    MDagPath dagPath;
    if( MStatus::kFailure == itDag.getPath(dagPath) ){
      MGlobal::displayError("MDagPath::getPath");
      return MStatus::kFailure;
    }

    MFnDagNode visibleTester( dagPath );
    if( isVisible( visibleTester, status ) && MStatus::kSuccess == status ){
      if( MStatus::kFailure == processMesh( dagPath ) )
        continue;
    }
    itDag.next();
  }
  return MStatus::kSuccess;
}

//--------------------------------------------------------------------------------------------------
MStatus prepareXMLWithSelection( void ){
  MGlobal::displayInfo("prepareXMLWithSelection");
  MSelectionList selectionList;
  if(MStatus::kFailure == MGlobal::getActiveSelectionList(selectionList)){
    MGlobal::displayError("MGlobal::getActiveSelectionList");
    return MStatus::kFailure;
  }
  MStatus status;
  MItSelectionList itSelectionList(selectionList, MFn::kMesh, &status);	
  if(MStatus::kFailure == status)
    return MStatus::kFailure;

  for( itSelectionList.reset(); !itSelectionList.isDone(); itSelectionList.next() ){
    MDagPath dagPath;
    if(MStatus::kFailure == itSelectionList.getDagPath(dagPath)) {
      MGlobal::displayError("MItSelectionList::getDagPath");
      return MStatus::kFailure;
    }
    if( MStatus::kFailure == processMesh( dagPath ) )
      continue;
  }
  return MStatus::kSuccess;
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

  if( MStatus::kSuccess == status )
    MGlobal::displayInfo("Export to " + fullFileName + " successful!");
  else
    MGlobal::displayInfo("Failed export to " + fullFileName + " successful!");
	return status;
}

//--------------------------------------------------------------------------------------------------
MPxFileTranslator::MFileKind ChaosExport::identifyFile( const MFileObject &file, const char * , short )const{
  MString name = file.name();
  int nameLength = name.length();
  int extensionLength = extension.length();
  if ( nameLength > extensionLength )
    if( name.substring( nameLength - extensionLength, nameLength ) == extension )
      return kIsMyFileType;
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
  if( !status )
    status.perror( "registerFileTranslator" );
  return status;
}

//--------------------------------------------------------------------------------------------------
MStatus uninitializePlugin( MObject obj ){
  MFnPlugin plugin( obj );
  MStatus status = plugin.deregisterFileTranslator( "chaosExport" );
  if( !status )
    status.perror( "deregisterFileTranslator" );
  return status;
}

//--------------------------------------------------------------------------------------------------
