/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU Affero General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/
#include "ProofOfWork.h"
#include "../cryptoplugin/ICryptoFactory.h"
#include "../Interface/Server.h"
#include "stringtools.h"

extern ICryptoFactory *crypto_fak;

unsigned int get_num_zero_bits(const std::string &str)
{
    unsigned int num_bits=0;
    for(size_t i=0; i<str.size(); i++)
    {
        unsigned char c=str[i];
        if(c==0)
        {
            num_bits+=8;
            continue;
        }
        for(int j=0; j<8; j++)
        {
            if(c&0x80)
                return num_bits;
            ++num_bits;
            c<<=1;
        }
    }
    return num_bits;
}

std::string perform_proof_of_work(const std::string &challenge, unsigned int difficulty)
{
    int64 salt = 0;
    std::string salt_str;
    salt_str.resize(sizeof(salt));
    
    while(true)
    {
        salt_str.assign((char*)&salt, sizeof(salt));
        std::string hash = crypto_fak->generateBinaryPasswordHash(challenge, salt_str, 1);
        if(get_num_zero_bits(hash)>=difficulty)
        {
            return salt_str;
        }
        ++salt;
    }
}

bool verify_proof_of_work(const std::string &challenge, const std::string &proof, unsigned int difficulty)
{
    std::string hash = crypto_fak->generateBinaryPasswordHash(challenge, proof, 1);
    return get_num_zero_bits(hash) >= difficulty;
}

bool proof_of_work_test()
{
    const std::string challenge = "test";
    const unsigned int difficulty = 24;
    const unsigned int starttime = Server->getTimeMS();
    const std::string proof = perform_proof_of_work(challenge, difficulty);

    Server->Log("Proof of work runtime "+PrettyPrintTime(int64(Server->getTimeMS()-starttime))+" difficulty "+convert(difficulty), LL_INFO);

    if(verify_proof_of_work(challenge, "abc", difficulty))
    {
        return false;
    }

    return verify_proof_of_work(challenge, proof, difficulty);
}