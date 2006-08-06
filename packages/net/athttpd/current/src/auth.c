/* =================================================================
 *
 *      auth.c
 *
 *      Handles basic authentication.
 *
 * ================================================================= 
 * ####ECOSGPLCOPYRIGHTBEGIN####
 * -------------------------------------------
 * This file is part of eCos, the Embedded Configurable Operating
 * System.
 * Copyright (C) 2005 eCosCentric Ltd.
 * 
 * eCos is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 or (at your option)
 * any later version.
 * 
 * eCos is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with eCos; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 * 
 * As a special exception, if other files instantiate templates or
 * use macros or inline functions from this file, or you compile this
 * file and link it with other works to produce a work based on this
 * file, this file does not by itself cause the resulting work to be
 * covered by the GNU General Public License. However the source code
 * for this file must still be made available in accordance with
 * section (3) of the GNU General Public License.
 * 
 * This exception does not invalidate any other reasons why a work
 * based on this file might be covered by the GNU General Public
 * License.
 *
 * -------------------------------------------
 * ####ECOSGPLCOPYRIGHTEND####
 * =================================================================
 * #####DESCRIPTIONBEGIN####
 * 
 *  Author(s):    Anthony Tonizzo (atonizzo@gmail.com)
 *  Contributors: 
 *  Date:         2006-06-12
 *  Purpose:      
 *  Description:  
 *               
 * ####DESCRIPTIONEND####
 * 
 * =================================================================
 */
#include <pkgconf/hal.h>
#include <pkgconf/kernel.h>
#include <cyg/kernel/kapi.h>           // Kernel API.
#include <cyg/hal/hal_tables.h>

#include <stdio.h>

#include <network.h>
#include <string.h>

#include <cyg/athttpd/http.h>
#include <cyg/athttpd/md5.h>

// This is the domain that is currently authorized.
cyg_uint8 *cyg_httpd_current_authName;

CYG_HAL_TABLE_BEGIN(cyg_httpd_auth_table, httpd_auth_table );
CYG_HAL_TABLE_END(cyg_httpd_auth_table_end, httpd_auth_table );

__externC cyg_httpd_auth_table_entry cyg_httpd_auth_table[];
__externC cyg_httpd_auth_table_entry cyg_httpd_auth_table_end[];

// Variables used for authorization.
char cyg_httpd_md5_nonce[33];
char cyg_httpd_md5_digest[33];
char cyg_httpd_md5_response[33];
char cyg_httpd_md5_cnonce[33];
char cyg_httpd_md5_noncecount[9];
char cyg_httpd_md5_ha2[HASHHEXLEN+1] = {'\0'};
char cyg_httpd_md5_ha1[HASHHEXLEN+1];

char b64string[] =
	"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

cyg_httpd_auth_table_entry*
cyg_httpd_auth_entry_from_path(char *authPath)
{
    cyg_httpd_auth_table_entry *entry = cyg_httpd_auth_table;
    if (strcmp(authPath, "/") != 0)
        while (entry != cyg_httpd_auth_table_end)
        {
            if (strncmp(entry->auth_dirname,
                        authPath,
                        strlen(entry->auth_dirname)) == 0)
                return entry;
            entry++;
        }
    else
        while (entry != cyg_httpd_auth_table_end)
        {
            if (strcmp(entry->auth_dirname, authPath) == 0)
                return entry;
            entry++;
        }
            
    return (cyg_httpd_auth_table_entry *)0;
}

cyg_httpd_auth_table_entry*
cyg_httpd_auth_entry_from_domain(char *authDomain)
{
    cyg_httpd_auth_table_entry *entry = cyg_httpd_auth_table;
    while (entry != cyg_httpd_auth_table_end)
    {
        if (!strncmp((const char*)authDomain, 
                       entry->auth_domainname,
                       strlen(entry->auth_domainname)))
            return entry;
        entry++;
    }
            
    return (cyg_httpd_auth_table_entry *)0;
}

cyg_int32
cyg_httpd_base64_encode(char* to, char* from, cyg_uint32 len )
{
	char     *fromp = from;
	char     *top = to;
	char      cbyte;
	char      obyte;
	cyg_int8  end[3];

	for (; len >= 3; len -= 3)
    {
		cbyte = *fromp++;
		*top++ = b64string[(int)(cbyte >> 2)];
		obyte = (cbyte << 4) & 0x30;

		cbyte = *fromp++;
		obyte |= (cbyte >> 4);		
		*top++ = b64string[(cyg_int32)obyte];
		obyte = (cbyte << 2) & 0x3C;

		cbyte = *fromp++;
		obyte |= (cbyte >> 6);		
		*top++ = b64string[(cyg_int32)obyte];
		*top++ = b64string[(cyg_int32)(cbyte & 0x3F)];
	}

	if (len)
    {
		end[0] = *fromp++;
		if (--len )
            end[1] = *fromp++; 
        else 
            end[1] = 0;
		end[2] = 0;

		cbyte = end[0];
		*top++ = b64string[(cyg_int32)(cbyte >> 2)];
		obyte = (cbyte << 4) & 0x30;

		cbyte = end[1];
		obyte |= (cbyte >> 4);
		*top++ = b64string[(cyg_int32)obyte];
		obyte = (cbyte << 2) & 0x3C;

		if (len )
            *top++ = b64string[(cyg_int32)obyte];
		else 
            *top++ = '=';
		*top++ = '=';
	}
	*top = 0;
	return top - to;
}

cyg_int32
cyg_httpd_base64_decode(char* to, char* from, cyg_uint32 len )
{
    char     *fromp = from;
    char     *top = to;
    char     *p;
    char      cbyte;
    char      obyte;
    cyg_int32 padding = 0;

	for (; len >= 4; len -= 4)
    {
        if ((cbyte = *fromp++) == '=')
            cbyte = 0;
		else
        {
            if (badchar(cbyte, p ) )
                return -1;
            cbyte = (p - b64string);
        }
        obyte = cbyte << 2;

        if ((cbyte = *fromp++) == '=')
            cbyte = 0;
		else
        {
			if (badchar(cbyte, p))
                return -1;
            cbyte = p - b64string;
		}
        obyte |= cbyte >> 4;
        *top++ = obyte;

        obyte = cbyte << 4;
        if ((cbyte = *fromp++) == '=')
        {
            cbyte = 0; 
            padding++;
        }
		else
        {
            padding = 0;
            if (badchar(cbyte, p))
                return -1;
            cbyte = p - b64string;
        }
        obyte |= cbyte >> 2;
        *top++ = obyte;

        obyte = cbyte << 6;
        if ((cbyte = *fromp++) == '=')
        {
            cbyte = 0;
            padding++;
        }
        else
        {
            padding = 0;
            if (badchar(cbyte, p))
                return -1;
            cbyte = p - b64string;
        }
        obyte |= cbyte;
        *top++ = obyte;
    }

    *top = 0;
    if (len)
        return -1;
    return (top - to) - padding;
}

cyg_httpd_auth_table_entry*
cyg_httpd_verify_auth(char* username, char* password)
{
    if ((strcmp(httpstate.needs_auth->auth_username, username) == 0) &&
        (strcmp(httpstate.needs_auth->auth_password, password) == 0))
        return httpstate.needs_auth;
    else    
        return (cyg_httpd_auth_table_entry*)0;
}

// The following code is a slightly modified version of those available at the
//  end of RFC1270.
void cyg_httpd_cvthex(HASH Bin, HASHHEX Hex)
{
    unsigned short i;
    unsigned char j;

    for (i = 0; i < HASHLEN; i++)
    {
        j = (Bin[i] >> 4) & 0xf;
        if (j <= 9)
            Hex[i*2] = (j + '0');
         else
            Hex[i*2] = (j + 'a' - 10);
        j = Bin[i] & 0xf;
        if (j <= 9)
            Hex[i*2+1] = (j + '0');
         else
            Hex[i*2+1] = (j + 'a' - 10);
    };
    Hex[HASHHEXLEN] = '\0';
};

// Calculate H(A1) as per spec.
void
cyg_httpd_digest_calc_HA1( char    *pszAlg,
                           char    *pszUserName,
                           char    *pszRealm,
                           char    *pszPassword,
                           char    *pszNonce,
                           char    *pszCNonce,
                           HASHHEX  SessionKey )
{
      MD5_CTX Md5Ctx;
      HASH HA1;

      MD5Init(&Md5Ctx);
      MD5Update(&Md5Ctx, (unsigned char*)pszUserName, strlen(pszUserName));
      MD5Update(&Md5Ctx, (unsigned char*)":", 1);
      MD5Update(&Md5Ctx, (unsigned char*)pszRealm, strlen(pszRealm));
      MD5Update(&Md5Ctx, (unsigned char*)":", 1);
      MD5Update(&Md5Ctx, (unsigned char*)pszPassword, strlen(pszPassword));
      MD5Final((unsigned char*)HA1, &Md5Ctx);
      if (strcmp(pszAlg, "md5-sess") == 0) 
      {
          MD5Init(&Md5Ctx);
          MD5Update(&Md5Ctx, (unsigned char*)HA1, HASHLEN);
          MD5Update(&Md5Ctx, (unsigned char*)":", 1);
          MD5Update(&Md5Ctx, (unsigned char*)pszNonce, strlen(pszNonce));
          MD5Update(&Md5Ctx, (unsigned char*)":", 1);
          MD5Update(&Md5Ctx, (unsigned char*)pszCNonce, strlen(pszCNonce));
          MD5Final((unsigned char*)HA1, &Md5Ctx);
      };
      cyg_httpd_cvthex(HA1, SessionKey);
};

// Calculate request-digest/response-digest as per HTTP Digest spec.
void
cyg_httpd_digest_calc_response(HASHHEX  HA1,           
                               char    *pszNonce,       
                               char    *pszNonceCount,  
                               char    *pszCNonce,      
                               char    *pszQop,         
                               char    *pszMethod,      
                               char    *pszDigestUri,   
                               HASHHEX  HEntity,       
                               HASHHEX  Response)
{
    MD5_CTX Md5Ctx;
    HASH HA2;
    HASH RespHash;
    HASHHEX HA2Hex;

    // Calculate H(A2).
    MD5Init(&Md5Ctx);
    MD5Update(&Md5Ctx, (unsigned char*)pszMethod, strlen(pszMethod));
    MD5Update(&Md5Ctx, (unsigned char*)":", 1);
    MD5Update(&Md5Ctx, (unsigned char*)pszDigestUri, strlen(pszDigestUri));
    if (strcmp(pszQop, "auth-int") == 0) {
        MD5Update(&Md5Ctx, (unsigned char*)":", 1);
        MD5Update(&Md5Ctx, (unsigned char*)HEntity, HASHHEXLEN);
    };
    MD5Final((unsigned char*)HA2, &Md5Ctx);
    cyg_httpd_cvthex(HA2, HA2Hex);

    // calculate response
    MD5Init(&Md5Ctx);
    MD5Update(&Md5Ctx, (unsigned char*)HA1, HASHHEXLEN);
    MD5Update(&Md5Ctx, (unsigned char*)":", 1);
    MD5Update(&Md5Ctx, (unsigned char*)pszNonce, strlen(pszNonce));
    MD5Update(&Md5Ctx, (unsigned char*)":", 1);
    if (*pszQop) 
    {
        MD5Update(&Md5Ctx, (unsigned char*)pszNonceCount, strlen(pszNonceCount));
        MD5Update(&Md5Ctx, (unsigned char*)":", 1);
        MD5Update(&Md5Ctx, (unsigned char*)pszCNonce, strlen(pszCNonce));
        MD5Update(&Md5Ctx, (unsigned char*)":", 1);
        MD5Update(&Md5Ctx, (unsigned char*)pszQop, strlen(pszQop));
        MD5Update(&Md5Ctx, (unsigned char*)":", 1);
    };
    MD5Update(&Md5Ctx, (unsigned char*)HA2Hex, HASHHEXLEN);
    MD5Final((unsigned char*)RespHash, &Md5Ctx);
    cyg_httpd_cvthex(RespHash, Response);
};

cyg_httpd_auth_table_entry*
cyg_httpd_is_authenticated(char* fname)
{
    // Let's check if the directory access needs authorization. The 
    //  authentication is done on the directory name.
    cyg_httpd_auth_table_entry* entry =
                                cyg_httpd_auth_entry_from_path(fname);
    if (entry != 0)
    {
        if (entry->auth_mode == CYG_HTTPD_AUTH_BASIC)
        {
            cyg_httpd_base64_decode(cyg_httpd_md5_response,
                                    cyg_httpd_md5_digest,
                                    strlen(cyg_httpd_md5_digest));
            char *extension = rindex(cyg_httpd_md5_response, ':');
            if (extension == NULL)
            {
                return (httpstate.needs_auth = entry);
            }    
            else
            {    
                *extension = '\0'; // Crypto now has the username.
                
                // In the case of a 'Basic" authentication, the HTTP header
                //  did not return to us the domain name that we sent when we
                //  challenged the request: The only things that are returned 
                //  are the username:password duo. In this case I will just 
                //  compare the entry's username/password to those read from 
                //  the header.
                if ((strcmp(entry->auth_username, 
                            cyg_httpd_md5_response) != 0) ||
                    (strcmp(entry->auth_password, 
                            ++extension) != 0))
                    return (httpstate.needs_auth = entry);
            }    
        }
        else
        {
            char *cyg_httpd_md5_method;
            
            switch (httpstate.method)
            {
            case CYG_HTTPD_METHOD_GET:
                cyg_httpd_md5_method = "GET";
                break;
            case CYG_HTTPD_METHOD_POST:
                cyg_httpd_md5_method = "POST";
                break;
            default:
                cyg_httpd_md5_method = "HEAD";
                break;
            }    
            cyg_httpd_digest_calc_HA1(CYG_HTTPD_MD5_AUTH_NAME,
                                      entry->auth_username,
                                      entry->auth_domainname,
                                      entry->auth_password,
                                      cyg_httpd_md5_nonce,
                                      cyg_httpd_md5_cnonce,
                                      cyg_httpd_md5_ha1);
            cyg_httpd_digest_calc_response(cyg_httpd_md5_ha1,
                                           cyg_httpd_md5_nonce,
                                           cyg_httpd_md5_noncecount,
                                           cyg_httpd_md5_cnonce,
                                           CYG_HTTPD_MD5_AUTH_QOP,
                                           cyg_httpd_md5_method,
                                           httpstate.url,
                                           cyg_httpd_md5_ha2,
                                           cyg_httpd_md5_digest);
            if (strcmp(cyg_httpd_md5_response, cyg_httpd_md5_digest) != 0)
                return (httpstate.needs_auth = entry);
        }
    }
    // No need for authentication...
    return (cyg_httpd_auth_table_entry*)0;
}

char*
cyg_httpd_digest_data(char *dest, char *src)
{
    int exit = 0;
    while (exit == 0)
    {
        switch (*src )
        {
        case '\r':
        case '\n':
            *dest = '\0';
            exit = 1;
            break;
        case ' ':
            src++;
            *dest = '\0';
            exit = 1;
            break;
        case '"':
        case ',':
            src++;
            break;
        default:
            *dest++ = *src++;
        }    
    }
    return src;
}    

// Skips through fields we do not need.
char*
cyg_httpd_digest_skip(char *p)
{
    if (*p == '"')
    {
        p++;
        while ((*p != '"') && (*p != '\n'))
            p++;
        p++;
        if (*p == ',')
            p++;
        if (*p == ' ')
            p++;
        if (*p == '\n')
            p++;
    }
    else        
    {    
        while ((*p != ' ') && (*p != '\n'))
            p++;
        if (*p == ',')
            p++;
        if (*p == ' ')
            p++;
        if (*p == '\n')
            p++;
    }        
    return p;
}