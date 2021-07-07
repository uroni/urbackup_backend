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
 * Abstract class for computeing crc checksums. Subclasses should implement computeCrc and computeCrcDirect.
 */ 
public abstract class AWSCRCAbstract implements Checksum {

    // 128 MiB
    static final int MAX_BUFFER_SIZE = 128 * 1024 * 1024;

    private int crc = 0;

    static {
        AWSChecksumsLoader.reload();
    }

    @Override
    public void update(int i) {
        byte[] bytes = new byte[1];
        bytes[0] = (byte)(i & 0xFF);
        crc = computeCrc(bytes, 0, 1, crc);
    }

    @Override
    public void update(byte[] bytes, int i, int i1) {
        //to prevent a user passing a large buffer and locking the VM, if the buffer is larger than 128 mb
        //slice it and do smaller segments at a time.
        int pos = 0;
        while(pos <  i1) {
            int toWrite = Math.min(i1 - pos, MAX_BUFFER_SIZE);
            crc = computeCrc(bytes, pos + i, toWrite, crc);
            pos += toWrite;
        }
    }

    public void update(ByteBuffer byteBuffer) {

        if(byteBuffer.isDirect()) {
            //slice if larger than 128 mb
            int pos = 0;
            int lengthToReadFromBuffer = byteBuffer.remaining();
            while(pos < lengthToReadFromBuffer) {
                int lengthToWrite = Math.min(lengthToReadFromBuffer - pos, MAX_BUFFER_SIZE);
                crc = computeCrcDirect(byteBuffer, pos + byteBuffer.position(), lengthToWrite, crc);
                pos += lengthToWrite;
            }
        }
        else if (byteBuffer.hasArray()) {
            update(byteBuffer.array(), byteBuffer.arrayOffset() + byteBuffer.position(), byteBuffer.remaining());
        }
        else {
             // Non-direct, but without (access to) backing array
             // i.e. heap byte buffer that is read-only.
             // Only choice is to make a copy.
             byte[] copy = new byte[byteBuffer.remaining()];
             byteBuffer.get(copy);
             update(copy, 0, copy.length);
       }
    }

    @Override
    public long getValue() {
        return crc;
    }

    @Override
    public void reset() {
        crc = 0;
    }

    /**
     * Takes a byte array, pos in that array, and length of the array and computes a crc on that data.
     */
    protected abstract int computeCrc(byte[] buffer, int pos, int length, int previousCrc);

    /**
     * Takes a nio ByteBuffer that was direct allocated, pos in the buffer, and length of the buffer  and computes a crc on that data.
     */
    protected abstract int computeCrcDirect(ByteBuffer buffer, int pos, int length, int previousCrc);
}

