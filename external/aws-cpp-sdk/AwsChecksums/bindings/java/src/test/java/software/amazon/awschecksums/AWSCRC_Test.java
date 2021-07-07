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


import org.junit.Test;

import java.nio.ByteBuffer;
import java.util.Random;

import static junit.framework.Assert.assertEquals;

public class AWSCRC_Test {
    private static final int KB_1 = 1024;
    private static final int MB_1 = 1024 * KB_1;
    private static final int GB_1 = 1024 * MB_1;

    byte[] data32Values = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
            17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 };

    byte[] data32test = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };

    @Test
    public void verifyCRC32c() {
        int knownValue = 0x46DD794E;

        AWSCRC32C crc =  new AWSCRC32C();
        crc.update(data32Values, 0, data32Values.length);
        assertEquals(knownValue, crc.getValue());
    }

    @Test
    public void verifyCRC32cNio() {

        ByteBuffer byteBuffer = ByteBuffer.allocateDirect(data32Values.length);
        byteBuffer.put(data32Values);
        byteBuffer.position(0);

        int knownValue = 0x46DD794E;

        AWSCRC32C crc =  new AWSCRC32C();
        crc.update(byteBuffer);
        assertEquals(knownValue, crc.getValue());
    }

    //the assumption here is that since we've already verified that the case with buffers < 128 MiB is correct using known
    //values, we can compute the crc using buffers smaller than 128 MiB, then compare that to the same data with buffers larger than 
    //128 MB, if they match the slicing is correct.
    @Test
    public void verifyAbstractCRCSlicing() {
        int noSlicingSize = AWSCRCAbstract.MAX_BUFFER_SIZE;
        int slicingSize = noSlicingSize * 2;
        int totalDataSize = slicingSize * 2;
        byte[] totalData = new byte[totalDataSize];
        Random rand = new Random();
        rand.nextBytes(totalData);
         
        int notSlicesCrc;
        int read = 0;

        AWSCRC32C noSlicesCrc = new AWSCRC32C();

        while(read < totalDataSize){
            noSlicesCrc.update(totalData, read, noSlicingSize);
            read += noSlicingSize;
        }

        AWSCRC32C slicesCrc = new AWSCRC32C();

        read = 0;
        while(read < totalDataSize) {
            slicesCrc.update(totalData, read, slicingSize);
            read += slicingSize;
        }

        assertEquals(noSlicesCrc.getValue(), slicesCrc.getValue());
    }

    //the assumption here is that since we've already verified that the case with buffers < 128 MiB is correct using known
    //values, we can compute the crc using buffers smaller than 128 MiB, then compare that to the same data with buffers larger than
    //128 MB, if they match the slicing is correct
    @Test
    public void verifyAbstractCRCSlicingNio() {
        int noSlicingSize = AWSCRCAbstract.MAX_BUFFER_SIZE;
        int slicingSize = noSlicingSize * 2;
        int totalDataSize = slicingSize * 2;
        byte[] totalData = new byte[totalDataSize];
        Random rand = new Random();
        rand.nextBytes(totalData);

        int notSlicesCrc;
        int read = 0;

        AWSCRC32C noSlicesCrc = new AWSCRC32C();

        ByteBuffer buffer = ByteBuffer.allocateDirect(noSlicingSize);
        while(read < totalDataSize){
            buffer.position(0);
            buffer.put(totalData, read, noSlicingSize);
            noSlicesCrc.update(buffer);
            read += noSlicingSize;
        }

        AWSCRC32C slicesCrc = new AWSCRC32C();

        read = 0;
        buffer = ByteBuffer.allocateDirect(slicingSize);
        while(read < totalDataSize) {
            buffer.position(0);
            buffer.put(totalData, read, slicingSize);
            slicesCrc.update(buffer);
            read += slicingSize;
        }

        assertEquals(noSlicesCrc.getValue(), slicesCrc.getValue());
    }

    @Test
    public void testCRC32CThroughput() {
        int amountComputed = 0;
        int dataSize = 1024;

        byte[] dataArray = new byte[dataSize];

        Random rand = new Random();
        rand.nextBytes(dataArray);

        ByteBuffer dataToWrite = ByteBuffer.allocateDirect(dataSize);
        dataToWrite.put(dataArray);
        dataToWrite.position(0);

        AWSCRC32C crc = new AWSCRC32C();

        long start = System.currentTimeMillis();

        while (amountComputed < KB_1)
        {
            crc.update(dataToWrite);
            amountComputed += dataSize;
        }

        long end = System.currentTimeMillis();
        crc.reset();

        System.out.println(String.format("CRC32C, 1KB, computation time: %d milliseconds.", end - start));

        start = System.currentTimeMillis();
        amountComputed = 0;

        while (amountComputed < MB_1)
        {
            crc.update(dataToWrite);
            amountComputed += dataSize;
        }

        end = System.currentTimeMillis();
        crc.reset();
        System.out.println(String.format("CRC32C, 1MB, computation time: %d milliseconds.", end - start));

        start = System.currentTimeMillis();
        amountComputed = 0;

        while (amountComputed < GB_1)
        {
            crc.update(dataToWrite);
            amountComputed += dataSize;
        }

        end = System.currentTimeMillis();
        crc.reset();
        System.out.println(String.format("CRC32C, 1GB, computation time: %d milliseconds.", end - start));
    }

    @Test
    public void verifyCRC32() {
        int knownValue = 0xCBF43926;

        AWSCRC32 crc =  new AWSCRC32();
        crc.update(data32test, 0, data32test.length);
        assertEquals(knownValue, crc.getValue());
    }

    @Test
    public void verifyCRC32Nio() {

        ByteBuffer byteBuffer = ByteBuffer.allocateDirect(data32test.length);
        byteBuffer.put(data32test);
        byteBuffer.position(0);

        int knownValue = 0xCBF43926;

        AWSCRC32 crc =  new AWSCRC32();
        crc.update(byteBuffer);
        assertEquals(knownValue, crc.getValue());
    }

    @Test
    public void testCRC32Throughput() {
        int amountComputed = 0;
        int dataSize = 1024;

        byte[] dataArray = new byte[dataSize];

        Random rand = new Random();
        rand.nextBytes(dataArray);

        ByteBuffer dataToWrite = ByteBuffer.allocateDirect(dataSize);
        dataToWrite.put(dataArray);
        dataToWrite.position(0);

        AWSCRC32 crc = new AWSCRC32();

        long start = System.currentTimeMillis();

        while (amountComputed < KB_1)
        {
            crc.update(dataToWrite);
            amountComputed += dataSize;
        }

        long end = System.currentTimeMillis();
        crc.reset();

        System.out.println(String.format("CRC32, 1KB, computation time: %d milliseconds.", end - start));

        start = System.currentTimeMillis();
        amountComputed = 0;

        while (amountComputed < MB_1)
        {
            crc.update(dataToWrite);
            amountComputed += dataSize;
        }

        end = System.currentTimeMillis();
        crc.reset();
        System.out.println(String.format("CRC32, 1MB, computation time: %d milliseconds.", end - start));

        start = System.currentTimeMillis();
        amountComputed = 0;

        while (amountComputed < GB_1)
        {
            crc.update(dataToWrite);
            amountComputed += dataSize;
        }

        end = System.currentTimeMillis();
        crc.reset();
        System.out.println(String.format("CRC32, 1GB, computation time: %d milliseconds.", end - start));
    }
}
