//
//  Copyright (c) 1994, 1995, 2006 by Mike Romberg ( mike.romberg@noaa.gov )
//
//  This file may be distributed under terms of the GPL
//

#include "swapmeter.h"
#include "xosview.h"
#include <fstream>
#include <sstream>
#include <stdlib.h>

static const char MEMFILENAME[] = "/proc/meminfo";


SwapMeter::SwapMeter( XOSView *parent )
: FieldMeterGraph( parent, 2, "SWAP", "USED/FREE" ){

}

SwapMeter::~SwapMeter( void ){
}

void SwapMeter::checkResources( void ){
  FieldMeterGraph::checkResources();

  setfieldcolor( 0, parent_->getResource( "swapUsedColor" ) );
  setfieldcolor( 1, parent_->getResource( "swapFreeColor" ) );
  priority_ = atoi (parent_->getResource( "swapPriority" ) );
  dodecay_ = parent_->isResourceTrue( "swapDecay" );
  useGraph_ = parent_->isResourceTrue( "swapGraph" );
  SetUsedFormat (parent_->getResource("swapUsedFormat"));
}

void SwapMeter::checkevent( void ){
  getswapinfo();
  drawfields();
}


void SwapMeter::getswapinfo( void ){
  std::ifstream meminfo( MEMFILENAME );
  if ( !meminfo ){
    std::cerr <<"Cannot open file : " <<MEMFILENAME << std::endl;
    exit( 1 );
  }

  total_ = fields_[0] = fields_[1] = 0;

  char buf[256];
  std::string ignore, unit;

  // Get the info from the "standard" meminfo file.
  while (!meminfo.eof()){
    meminfo.getline(buf, 256);
    std::istringstream line(std::string(buf, 256));

    if(!strncmp("SwapTotal", buf, strlen("SwapTotal"))){
      line >> ignore >> total_ >> unit;
      if (strncasecmp(unit.c_str(), "kB", 2) == 0)
        total_ *= 1024.0;
      if (strncasecmp(unit.c_str(), "MB", 2) == 0)
        total_ *= 1024.0*1024.0;
      if (strncasecmp(unit.c_str(), "GB", 2) == 0)
        total_ *= 1024.0*1024.0*1024.0;
    }
    if(!strncmp("SwapFree", buf, strlen("SwapFree"))){
      line >> ignore >> fields_[1] >> unit;
      if (strncasecmp(unit.c_str(), "kB", 2) == 0)
        fields_[1] *= 1024.0;
      if (strncasecmp(unit.c_str(), "MB", 2) == 0)
        fields_[1] *= 1024.0*1024.0;
      if (strncasecmp(unit.c_str(), "GB", 2) == 0)
        fields_[1] *= 1024.0*1024.0*1024.0;
    }
  }

  fields_[0] = total_ - fields_[1];

  if ( total_ == 0 ){
    total_ = 1;
    fields_[0] = 0;
    fields_[1] = 1;
  }

  if (total_)
    setUsed (fields_[0], total_);
}
