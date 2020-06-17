/*
 * Copyright (C) 2017-2020 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __GCSS_PARSER_H__
#define __GCSS_PARSER_H__

#include "gcss.h"
#include "gcss_item.h"
#include <expat.h>

namespace GCSS {

/**
 * \class GCSSParser
 *
 * This class is used to parse the Graph Configuration Subsystem graph
 * descriptor xml file. Uses the expat lib to do the parsing.
 */
class GCSSParser {
public:
    GCSSParser();
    ~GCSSParser();
    void parseGCSSXmlFile(const char*, IGraphConfig**);
    void parseGCSSXmlData(char*, size_t, IGraphConfig**);

private: /* Constants*/
    static const int BUFFERSIZE = 4*1024;  // For xml file

private: /* Methods */
    GCSSParser(const GCSSParser& other);
    GCSSParser& operator=(const GCSSParser& other);

    static void startElement(void *userData, const char *name, const char **atts);
    static void endElement(void *userData, const char *name);

    void parseXML(XML_Parser &parser, const char* fileName, void* pBuf);
#ifndef ZLIB_DISABLED
    void parseGz(XML_Parser &parser, const char* filename, void* pBuf);
#endif
    void handleGraph(const char *name, const char **atts);
    void handleNode(const char *name, const char **atts);
    void checkField(GCSSParser *profiles, const char *name, const char **atts);

private:  /* Members */

    ia_uid mTopLevelNode;
    std::string mVersion; /* save version number from the xml here */
    GCSS::GraphConfigNode *mCurrentNode; /* TODO: these should be of GraphConfig-iface type */
};
} // namespace
#endif
