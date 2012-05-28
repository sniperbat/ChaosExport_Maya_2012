#ifndef _CHAOSEXPORT_H
#define _CHAOSEXPORT_H
//--------------------------------------------------------------------------------------------------
#include <maya/MPxFileTranslator.h>
#include <maya/MFStream.h>

//--------------------------------------------------------------------------------------------------
class ChaosExport : public MPxFileTranslator {
public:
	ChaosExport( void ){}
	~ChaosExport( void ){}
	MStatus writer( const MFileObject &file, const MString &optionsString, FileAccessMode mode );
	inline bool haveWriteMethod( void )const;
  inline bool haveReadMethod( void )const;
  bool canBeOpened( void ) const;
	MString defaultExtension( void ) const;
  MFileKind	identifyFile( const MFileObject &, const char *buffer, short size )const;
	inline static void * creator( void );
  
  enum Format{
    UNKNOWN_FORMAT = -1,
    XML_FORMAT,
    BINARY_FORMAT,
  };
};

//--------------------------------------------------------------------------------------------------
inline bool ChaosExport::haveWriteMethod( void ) const{
	return true;
}

//--------------------------------------------------------------------------------------------------
inline bool ChaosExport::haveReadMethod( void )const{
  return false;
}

//--------------------------------------------------------------------------------------------------
inline bool ChaosExport::canBeOpened( void ) const{
	return false;
}

//--------------------------------------------------------------------------------------------------
inline void * ChaosExport::creator( void ){
	return new ChaosExport;
}

//--------------------------------------------------------------------------------------------------

#endif//_CHAOSEXPORT_H
