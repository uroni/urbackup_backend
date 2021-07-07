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

using System;
using System.Text;
using System.Diagnostics;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace AWS.Checksums.Tests
{
    [TestClass()]
    public class CRCTest
    {
        [TestMethod()]
        public void TestCRC32CZeros()
        {
            byte[] data_32_zeroes = new byte[32];
            uint knownCrc32cZeros = 0x8A9136AA;

            using (var crc = new CRC32C())
            {
                byte[] res = crc.ComputeHash(data_32_zeroes);
                Assert.AreEqual(knownCrc32cZeros, BitConverter.ToUInt32(res, 0));                
            }
        }

        [TestMethod()]
        public void TestCRC32CValues()
        {
            byte[] data_32_values = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31 };
            uint knownValue = 0x46DD794E;

            using (var crc = new CRC32C())
            {
                byte[] res = crc.ComputeHash(data_32_values);
                Assert.AreEqual(knownValue, BitConverter.ToUInt32(res, 0));
            }
        }

        [TestMethod()]
        public void TestCRC32CTestVector()
        {
            char[] data_32_test = { '1', '2', '3', '4', '5', '6', '7', '8', '9' }; 
            uint knownValue = 0xE3069283;

            using (var crc = new CRC32C())
            {                
                byte[] res = crc.ComputeHash(Encoding.ASCII.GetBytes(data_32_test));
                Assert.AreEqual(knownValue, BitConverter.ToUInt32(res, 0));
            }
        }

        [TestMethod()]
        public void TestCRC32Zeros()
        {
            byte[] data_32_zeroes = new byte[32];
            uint knownCrc32cZeros = 0x190A55AD;

            using (var crc = new CRC32())
            {
                byte[] res = crc.ComputeHash(data_32_zeroes);
                Assert.AreEqual(knownCrc32cZeros, BitConverter.ToUInt32(res, 0));
            }
        }

        [TestMethod()]
        public void TestCRC32TestVector()
        {
            char[] data_32_test = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
            uint knownValue = 0xCBF43926;

            using (var crc = new CRC32())
            {
                byte[] res = crc.ComputeHash(Encoding.ASCII.GetBytes(data_32_test));
                Assert.AreEqual(knownValue, BitConverter.ToUInt32(res, 0));
            }
        }

        [TestMethod()]
        public void ComputeLatencyTimesCrc32c()
        {
            uint Kb1 = 1024;
            uint Mb1 = Kb1 * 1024;
            uint Gb1 = Mb1 * 1024;
            int amountComputed = 0;
            uint crcValue = 0;
            int dataSize = 1024;

            byte[] dataToWrite = new byte[dataSize];

            Random rand = new Random();
            rand.NextBytes(dataToWrite);

            var crc = new CRC32C();      

            Stopwatch sw = Stopwatch.StartNew();
            
            while (amountComputed < Kb1)
            {
                crcValue = crc.ComputeRunning(dataToWrite, dataSize, crcValue);
                amountComputed += dataSize;
            }

            long end = sw.ElapsedMilliseconds;
            Console.WriteLine(String.Format("CRC32C, 1KB, computation time: {0} milliseconds.", end));
            
            sw.Restart();
            amountComputed = 0;

            while (amountComputed < Mb1)
            {
                crcValue = crc.ComputeRunning(dataToWrite, dataSize, crcValue);
                amountComputed += dataSize;
            }

            end = sw.ElapsedMilliseconds;
            Console.WriteLine(String.Format("CRC32C, 1MB, computation time: {0} milliseconds.", end));

            sw.Restart();
            amountComputed = 0;

            while (amountComputed < Gb1)
            {
                crcValue = crc.ComputeRunning(dataToWrite, dataSize, crcValue);
                amountComputed += dataSize;
            }

            end = sw.ElapsedMilliseconds;
            Console.WriteLine(String.Format("CRC32C, 1GB, computation time: {0} milliseconds.", end));   
        }
    }
}
