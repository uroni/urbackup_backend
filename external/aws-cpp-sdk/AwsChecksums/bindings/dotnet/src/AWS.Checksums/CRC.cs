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
using System.Security.Cryptography;

namespace AWS.Checksums
{
    public abstract class CRC : HashAlgorithm
    {
        private uint currentCrc = 0;
        private uint lastComputedCRC = 0;
        private bool resetCalled = false;

        public override void Initialize()
        {
            resetCalled = true;
        }

        protected override void Dispose(bool disposing)
        {
            //no unmanaged resources here.
        }

        public byte[] LastComputedCRCAsBigEndian
        {
            get
            {
                if (BitConverter.IsLittleEndian)
                {
                    byte[] crcLE = HashFinal();
                    Array.Reverse(crcLE);
                    return crcLE;
                }
                else
                {
                    return HashFinal();
                }
            }
        }

        public abstract uint ComputeRunning(byte[] buffer, int length, uint previousCrc);
        
        protected override byte[] HashFinal()
        {
            return BitConverter.GetBytes(currentCrc);
        }

        protected override void HashCore(byte[] array, int ibStart, int cbSize)
        {
            if(resetCalled)
            {
                currentCrc = 0;
                resetCalled = false;
            }

            if (ibStart == 0)
            {
                currentCrc = ComputeRunning(array, cbSize, currentCrc);
            }
            else
            {
                byte[] array_cpy = new byte[cbSize];
                Buffer.BlockCopy(array, ibStart, array_cpy, 0, cbSize);
                currentCrc = ComputeRunning(array_cpy, cbSize, currentCrc);
            }
        }
    }
    public class CRC32C : CRC
    {
        public override uint ComputeRunning(byte[] buffer, int length, uint previousCrc)
        {
            return NativeInterop.AWSCRCNative.CRC32C(buffer, length, previousCrc);
        }

    }

    public class CRC32 : CRC
    {
        public override uint ComputeRunning(byte[] buffer, int length, uint previousCrc)
        {
            return NativeInterop.AWSCRCNative.CRC32(buffer, length, previousCrc);
        }
    }
}
