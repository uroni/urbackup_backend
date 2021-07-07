﻿/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/s3/model/IntelligentTieringFilter.h>
#include <aws/core/utils/xml/XmlSerializer.h>
#include <aws/core/utils/StringUtils.h>
#include <aws/core/utils/memory/stl/AWSStringStream.h>

#include <utility>

using namespace Aws::Utils::Xml;
using namespace Aws::Utils;

namespace Aws
{
namespace S3
{
namespace Model
{

IntelligentTieringFilter::IntelligentTieringFilter() : 
    m_prefixHasBeenSet(false),
    m_tagHasBeenSet(false),
    m_andHasBeenSet(false)
{
}

IntelligentTieringFilter::IntelligentTieringFilter(const XmlNode& xmlNode) : 
    m_prefixHasBeenSet(false),
    m_tagHasBeenSet(false),
    m_andHasBeenSet(false)
{
  *this = xmlNode;
}

IntelligentTieringFilter& IntelligentTieringFilter::operator =(const XmlNode& xmlNode)
{
  XmlNode resultNode = xmlNode;

  if(!resultNode.IsNull())
  {
    XmlNode prefixNode = resultNode.FirstChild("Prefix");
    if(!prefixNode.IsNull())
    {
      m_prefix = Aws::Utils::Xml::DecodeEscapedXmlText(prefixNode.GetText());
      m_prefixHasBeenSet = true;
    }
    XmlNode tagNode = resultNode.FirstChild("Tag");
    if(!tagNode.IsNull())
    {
      m_tag = tagNode;
      m_tagHasBeenSet = true;
    }
    XmlNode andNode = resultNode.FirstChild("And");
    if(!andNode.IsNull())
    {
      m_and = andNode;
      m_andHasBeenSet = true;
    }
  }

  return *this;
}

void IntelligentTieringFilter::AddToNode(XmlNode& parentNode) const
{
  Aws::StringStream ss;
  if(m_prefixHasBeenSet)
  {
   XmlNode prefixNode = parentNode.CreateChildElement("Prefix");
   prefixNode.SetText(m_prefix);
  }

  if(m_tagHasBeenSet)
  {
   XmlNode tagNode = parentNode.CreateChildElement("Tag");
   m_tag.AddToNode(tagNode);
  }

  if(m_andHasBeenSet)
  {
   XmlNode andNode = parentNode.CreateChildElement("And");
   m_and.AddToNode(andNode);
  }

}

} // namespace Model
} // namespace S3
} // namespace Aws
