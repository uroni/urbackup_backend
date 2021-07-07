﻿/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/s3/model/GetBucketCorsResult.h>
#include <aws/core/utils/xml/XmlSerializer.h>
#include <aws/core/AmazonWebServiceResult.h>
#include <aws/core/utils/StringUtils.h>

#include <utility>

using namespace Aws::S3::Model;
using namespace Aws::Utils::Xml;
using namespace Aws::Utils;
using namespace Aws;

GetBucketCorsResult::GetBucketCorsResult()
{
}

GetBucketCorsResult::GetBucketCorsResult(const Aws::AmazonWebServiceResult<XmlDocument>& result)
{
  *this = result;
}

GetBucketCorsResult& GetBucketCorsResult::operator =(const Aws::AmazonWebServiceResult<XmlDocument>& result)
{
  const XmlDocument& xmlDocument = result.GetPayload();
  XmlNode resultNode = xmlDocument.GetRootElement();

  if(!resultNode.IsNull())
  {
    XmlNode cORSRulesNode = resultNode.FirstChild("CORSRule");
    if(!cORSRulesNode.IsNull())
    {
      XmlNode cORSRuleMember = cORSRulesNode;
      while(!cORSRuleMember.IsNull())
      {
        m_cORSRules.push_back(cORSRuleMember);
        cORSRuleMember = cORSRuleMember.NextNode("CORSRule");
      }

    }
  }

  return *this;
}
