/*
* Copyright 2010-2017 Amazon.com, Inc. or its affiliates. All Rights Reserved.
*
* Licensed under the Apache License, Version 2.0 (the "License").
* You may not use this file except in compliance with the License.
* A copy of the License is located at
*
*  http://aws.amazon.com/apache2.0
*
* or in the "license" file accompanying this file. This file is distributed
* on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
* express or implied. See the License for the specific language governing
* permissions and limitations under the License.
*/

package software.amazon.awschecksums;

import java.nio.ByteBuffer;
import java.util.zip.Checksum;

/**
 * computes CRC32 using hw acceleration if possible.
 */
public class AWSCRC32 extends AWSCRCAbstract {

    @Override
    protected int computeCrc(byte[] data, int pos, int length, int previousCrc) {
        return crc32(data, pos, length, previousCrc);
    }

    @Override
    protected int computeCrcDirect(ByteBuffer data, int pos, int length, int previousCrc) {
        return crc32Direct(data, pos, length, previousCrc);
    }

    private native int crc32(byte[] data, int offset, int length, int previousCrc);

    private native int crc32Direct(ByteBuffer data, int offset, int length, int previousCrc);
}
