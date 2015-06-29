/*************************************************************************
*    UrBackup - Client/Server backup system
*    Copyright (C) 2011  Martin Raiber
*
*    This program is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
**************************************************************************/

#include "os_functions.h"
#include "../stringtools.h"
#include "server_compat.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <algorithm>
#include <memory.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <utime.h>

#if defined(__FreeBSD__) || defined(__APPLE__)
#define lstat64 lstat
#define stat64 stat
#define statvfs64 statvfs
#define open64 open
#endif

void getMousePos(int &x, int &y)
{
	x=0;
	y=0;
}

std::vector<SFile> getFilesWin(const std::wstring &path, bool *has_error, bool exact_filesize, bool with_usn)
{
	return getFiles(path, has_error);
}

std::vector<SFile> getFiles(const std::wstring &path, bool *has_error)
{
	if(has_error!=NULL)
	{
		*has_error=false;
	}
    std::string upath=ConvertToUTF8(path);
	std::vector<SFile> tmp;
	DIR *dp;
    struct dirent *dirp;
    if((dp  = opendir(upath.c_str())) == NULL)
	{
		if(has_error!=NULL)
		{
			*has_error=true;
		}
        Log("No permission to access \""+upath+"\"", LL_ERROR);
        return tmp;
    }
	
	upath+=os_file_sepn();

    while ((dirp = readdir(dp)) != NULL)
	{
		SFile f;
        f.name=ConvertToUnicode(dirp->d_name);
		if(f.name==L"." || f.name==L".." )
			continue;
		
#ifndef sun
		f.isdir=(dirp->d_type==DT_DIR);
		if(!f.isdir || dirp->d_type==DT_UNKNOWN 
				|| (dirp->d_type!=DT_REG && dirp->d_type!=DT_DIR)
				|| dirp->d_type==DT_LNK )
		{
#endif
			struct stat64 f_info;
			int rc=lstat64((upath+dirp->d_name).c_str(), &f_info);
			if(rc==0)
			{
				if(S_ISLNK(f_info.st_mode))
				{
					f.issym=true;
					f.isspecial=true;
					struct stat64 l_info;
					int rc2 = stat64((upath+dirp->d_name).c_str(), &l_info);
					
					if(rc2==0)
					{
						f.isdir=S_ISDIR(l_info.st_mode);
					}
				}
			
#ifndef sun
				if(dirp->d_type==DT_UNKNOWN
					|| (dirp->d_type!=DT_REG && dirp->d_type!=DT_DIR)
					|| dirp->d_type==DT_LNK)
				{
#endif
					if(!f.issym)
					{
						f.isdir=S_ISDIR(f_info.st_mode);
					}
					
					if(!f.isdir)
					{
						if(!S_ISREG(f_info.st_mode) )
						{
							f.isspecial=true;
						}
                        uint64 last_modified = (uint64)f_info.st_mtime | ((uint64)f_info.st_ctime<<32);
                        f.last_modified=last_modified;
						if(f.last_modified<0) f.last_modified*=-1;
						f.size=f_info.st_size;
					}
#ifndef sun
				}
				else
				{
                    uint64 last_modified = (uint64)f_info.st_mtime | ((uint64)f_info.st_ctime<<32);
                    f.last_modified=last_modified;
					if(f.last_modified<0) f.last_modified*=-1;
					f.size=f_info.st_size;
				}
#endif
			}
			else
			{
                    Log("Stat failed for \""+upath+dirp->d_name+"\" errno: "+nconvert(errno), LL_ERROR);
                    if(has_error!=NULL)
                    {
                        *has_error=true;
                    }
                    continue;
			}
#ifndef sun
		}
		else
		{
			f.last_modified=0;
			f.size=0;
		}
#endif
		tmp.push_back(f);
    }
    closedir(dp);
	
	std::sort(tmp.begin(), tmp.end());
	
    return tmp;
}

void removeFile(const std::wstring &path)
{
    unlink(ConvertToUTF8(path).c_str());
}

void moveFile(const std::wstring &src, const std::wstring &dst)
{
    rename(ConvertToUTF8(src).c_str(), ConvertToUTF8(dst).c_str() );
}

bool os_remove_symlink_dir(const std::wstring &path)
{
    return unlink(ConvertToUTF8(path).c_str())==0;
}

bool os_remove_dir(const std::string &path)
{
	return rmdir(path.c_str())==0;
}

bool os_remove_dir(const std::wstring &path)
{
    return rmdir(ConvertToUTF8(path).c_str())==0;
}

bool isDirectory(const std::wstring &path, void* transaction)
{
        struct stat64 f_info;
        int rc=stat64(ConvertToUTF8(path).c_str(), &f_info);
		if(rc!=0)
		{
            rc = lstat64(ConvertToUTF8(path).c_str(), &f_info);
			if(rc!=0)
			{
				return false;
			}
		}

        if ( S_ISDIR(f_info.st_mode) )
        {
                return true;
        }
        else
        {
                return false;
        }
}

int os_get_file_type(const std::wstring &path)
{
	int ret = 0;
	struct stat64 f_info;
    int rc1=stat64(ConvertToUTF8(path).c_str(), &f_info);
	if(rc1==0)
	{
		if ( S_ISDIR(f_info.st_mode) )
        {
			ret |= EFileType_Directory;
		}
		else
		{
			ret |= EFileType_File;
		}
	}

    int rc2 = lstat64(ConvertToUTF8(path).c_str(), &f_info);
	if(rc2==0)
	{
		if(S_ISLNK(f_info.st_mode))
		{
			ret |= EFileType_Symlink;
		}
		
		if(rc1!=0)
		{
			ret |= EFileType_File;
		}
	}
	
	return ret;
}

int64 os_atoi64(const std::string &str)
{
	return strtoll(str.c_str(), NULL, 10);
}

bool os_create_dir(const std::wstring &dir)
{
    int rc=mkdir(ConvertToUTF8(dir).c_str(), S_IRWXU | S_IRWXG );
	return rc==0;
}

bool os_create_dir(const std::string &path)
{
	return mkdir(path.c_str(), S_IRWXU | S_IRWXG)==0;
}

bool os_create_reflink(const std::wstring &linkname, const std::wstring &fname)
{
#ifndef sun
    int src_desc=open64(ConvertToUTF8(fname).c_str(), O_RDONLY);
	if( src_desc<0)
	{
        Log("Error opening source file. errno="+nconvert(errno), LL_INFO);
	    return false;
	}

    int dst_desc=open64(ConvertToUTF8(linkname).c_str(), O_WRONLY | O_CREAT | O_EXCL, S_IRWXU | S_IRWXG);
	if( dst_desc<0 )
	{
        Log("Error opening destination file. errno="+nconvert(errno), LL_INFO);
	    close(src_desc);
	    return false;
	}

#define BTRFS_IOCTL_MAGIC 0x94
#define BTRFS_IOC_CLONE _IOW (BTRFS_IOCTL_MAGIC, 9, int)
	
	int rc=ioctl(dst_desc, BTRFS_IOC_CLONE, src_desc);
	
	if(rc)
	{
        Log("Reflink ioctl failed. errno="+nconvert(errno), LL_INFO);
	}

	close(src_desc);
	close(dst_desc);
	
	if(rc)
	{
        if(unlink(ConvertToUTF8(linkname).c_str()))
		{
            Log("Removing destination file failed. errno="+nconvert(errno), LL_INFO);
		}
	}
	
	return rc==0;
#else
	return false;
#endif
}

bool os_create_hardlink(const std::wstring &linkname, const std::wstring &fname, bool use_ioref, bool* too_many_links)
{
	if(too_many_links!=NULL)
		*too_many_links=false;
		
	if( use_ioref )
		return os_create_reflink(linkname, fname);
		
    int rc=link(ConvertToUTF8(fname).c_str(), ConvertToUTF8(linkname).c_str());
	return rc==0;
}

int64 os_free_space(const std::wstring &path)
{
	std::wstring cp=path;
	if(path.size()==0)
		return -1;
	if(cp[cp.size()-1]=='/')
		cp.erase(cp.size()-1, 1);
	if(cp[cp.size()-1]!='/')
		cp+='/';

	struct statvfs64 buf;
    int rc=statvfs64(ConvertToUTF8(path).c_str(), &buf);
	if(rc==0)
		return buf.f_bsize*buf.f_bavail;
	else
		return -1;
}

int64 os_total_space(const std::wstring &path)
{
	std::wstring cp=path;
	if(path.size()==0)
		return -1;
	if(cp[cp.size()-1]=='/')
		cp.erase(cp.size()-1, 1);
	if(cp[cp.size()-1]!='/')
		cp+='/';

	struct statvfs64 buf;
    int rc=statvfs64(ConvertToUTF8(path).c_str(), &buf);
	if(rc==0)
	{
		fsblkcnt_t used=buf.f_blocks-buf.f_bfree;
		return buf.f_bsize*(used+buf.f_bavail);
	}
	else
		return -1;
}

bool os_directory_exists(const std::wstring &path)
{
    //std::string upath=ConvertToUTF8(path);
	//DIR *dp=opendir(upath.c_str());
	//closedir(dp);
	//return dp!=NULL;
	return isDirectory(path);
}

bool os_remove_nonempty_dir(const std::wstring &path, os_symlink_callback_t symlink_callback, void* userdata, bool delete_root)
{
    std::string upath=ConvertToUTF8(path);
	std::vector<SFile> tmp;
	DIR *dp;
    struct dirent *dirp;
    if((dp  = opendir(upath.c_str())) == NULL)
	{
        Log("No permission to access \""+upath+"\"", LL_ERROR);
        return false;
    }
	
	bool ok=true;
	std::vector<std::wstring> subdirs;
	while ((dirp = readdir(dp)) != NULL)
	{
		if( (std::string)dirp->d_name!="." && (std::string)dirp->d_name!=".." )
		{
#ifndef sun
			if(dirp->d_type==DT_UNKNOWN)
			{
#endif
				struct stat64 f_info;
				int rc=lstat64((upath+"/"+(std::string)dirp->d_name).c_str(), &f_info);
				if(rc==0)
				{
					if(S_ISDIR(f_info.st_mode) )
					{
                        subdirs.push_back(ConvertToUnicode(dirp->d_name));
					}
					else if(S_ISLNK(f_info.st_mode))
					{
						if(symlink_callback!=NULL)
						{
                            symlink_callback(ConvertToUnicode(upath+"/"+(std::string)dirp->d_name), userdata);
						}
						else
						{
							if(unlink((upath+"/"+(std::string)dirp->d_name).c_str())!=0)
							{
                                Log("Error deleting symlink \""+upath+"/"+(std::string)dirp->d_name+"\"", LL_ERROR);
							}
						}
					}
					else
					{
						if(unlink((upath+"/"+(std::string)dirp->d_name).c_str())!=0)
						{
                            Log("Error deleting file \""+upath+"/"+(std::string)dirp->d_name+"\"", LL_ERROR);
						}
					}
				}
				else
				{
					std::string e=nconvert(errno);
					switch(errno)
					{
					    case EACCES: e="EACCES"; break;
					    case EBADF: e="EBADF"; break;
					    case EFAULT: e="EFAULT"; break;
					    case ELOOP: e="ELOOP"; break;
					    case ENAMETOOLONG: e="ENAMETOOLONG"; break;
					    case ENOENT: e="ENOENT"; break;
					    case ENOMEM: e="ENOMEM"; break;
					    case ENOTDIR: e="ENOTDIR"; break;
					}
                    Log("No permission to stat \""+upath+"/"+dirp->d_name+"\" error: "+e, LL_ERROR);
				}
#ifndef sun
			}
			else if(dirp->d_type==DT_DIR )
			{
                subdirs.push_back(ConvertToUnicode(dirp->d_name));
			}
			else if(dirp->d_type==DT_LNK )
			{
				if(symlink_callback!=NULL)
				{
                    symlink_callback(ConvertToUnicode(upath+"/"+(std::string)dirp->d_name), userdata);
				}
				else
				{
					if(unlink((upath+"/"+(std::string)dirp->d_name).c_str())!=0)
					{
                        Log("Error deleting symlink \""+upath+"/"+(std::string)dirp->d_name+"\"", LL_ERROR);
					}
				}
			}
			else
			{
				if(unlink((upath+"/"+(std::string)dirp->d_name).c_str())!=0)
				{
                    Log("Error deleting file \""+upath+"/"+(std::string)dirp->d_name+"\"", LL_ERROR);
				}
			}
#endif
		}
    }
    closedir(dp);
    for(size_t i=0;i<subdirs.size();++i)
    {
		bool b=os_remove_nonempty_dir(path+L"/"+subdirs[i], symlink_callback, userdata);
		if(!b)
		    ok=false;
    }
	if(delete_root)
	{
		if(rmdir(upath.c_str())!=0)
		{
            Log("Error deleting directory \""+upath+"\"", LL_ERROR);
		}
	}
	return ok;
}

std::wstring os_file_sep(void)
{
	return L"/";
}

std::string os_file_sepn(void)
{
	return "/";
}

bool os_link_symbolic(const std::wstring &target, const std::wstring &lname, void* transaction, bool* isdir)
{
    return symlink(ConvertToUTF8(target).c_str(), ConvertToUTF8(lname).c_str())==0;
}

bool os_lookuphostname(std::string pServer, unsigned int *dest)
{
	const char* host=pServer.c_str();
    unsigned int addr = inet_addr(host);
    if (addr != INADDR_NONE)
	{
        *dest = addr;
    }
	else
	{
		addrinfo hints;
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;

		addrinfo* h;
		if(getaddrinfo(pServer.c_str(), NULL, &hints, &h)==0)
		{
			if(h!=NULL)
			{
				in_addr tmp;
				if(h->ai_addrlen>=sizeof(sockaddr_in))
				{
					*dest=reinterpret_cast<sockaddr_in*>(h->ai_addr)->sin_addr.s_addr;
					freeaddrinfo(h);
					return true;
				}				
				else
				{
					freeaddrinfo(h);
					return false;
				}
			}
			else
			{
				return false;
			}
		}
		else
		{
			return false;
		}
	}
	return true;
}

std::wstring os_file_prefix(std::wstring path)
{
	return path;
}

bool os_file_truncate(const std::wstring &fn, int64 fsize)
{
    if( truncate(ConvertToUTF8(fn).c_str(), (off_t)fsize) !=0 )
	{
		return false;
	}
	return true;
}

std::string os_strftime(std::string fs)
{
	time_t rawtime;		
	char buffer [100];
	time ( &rawtime );
	struct tm *timeinfo;
	timeinfo = localtime ( &rawtime );
	strftime (buffer,100,fs.c_str(),timeinfo);
	std::string r(buffer);
	return r;
}

bool os_create_dir_recursive(std::wstring fn)
{
	if(fn.empty())
		return false;
		
	bool b=os_create_dir(fn);
	if(!b)
	{
		b=os_create_dir_recursive(ExtractFilePath(fn));
		if(!b)
			return false;

		return os_create_dir(fn);
	}
	else
	{
		return true;
	}
}

bool os_rename_file(std::wstring src, std::wstring dst, void* transaction)
{
    int rc=rename(ConvertToUTF8(src).c_str(), ConvertToUTF8(dst).c_str());
	return rc==0;
}

bool os_get_symlink_target(const std::wstring &lname, std::wstring &target)
{
    std::string lname_utf8 = ConvertToUTF8(lname);
	struct stat sb;
	if(lstat(lname_utf8.c_str(), &sb)==-1)
	{
		return false;
	}
	
	std::string target_buf;
	
	target_buf.resize(sb.st_size);
	
	ssize_t rc = readlink(lname_utf8.c_str(), &target_buf[0], sb.st_size);
	
	if(rc<0)
	{
		return false;
	}
	
	if(rc > sb.st_size)
	{
		return false;
	}
	else if(rc<sb.st_size)
	{
		target_buf.resize(rc);
	}
	
    target = ConvertToUnicode(target_buf);
	
	return true;
}

bool os_is_symlink(const std::wstring &lname)
{
	struct stat sb;
    if(lstat(ConvertToUTF8(lname).c_str(), &sb)==-1)
	{
		return false;
	}
	
	return S_ISLNK(sb.st_mode);
}

void* os_start_transaction()
{
	return NULL;
}

bool os_finish_transaction(void* transaction)
{
	return false;
}

int64 os_last_error()
{
	return errno;
}

const int64 WINDOWS_TICK=10000000;
const int64 SEC_TO_UNIX_EPOCH=11644473600LL;

int64 os_windows_to_unix_time(int64 windows_filetime)
{	
	return windows_filetime / WINDOWS_TICK - SEC_TO_UNIX_EPOCH;
}

int64 os_to_windows_filetime(int64 unix_time)
{
	return (unix_time+SEC_TO_UNIX_EPOCH)*WINDOWS_TICK;
}

bool os_set_file_time(const std::wstring& fn, int64 created, int64 last_modified)
{
	struct utimbuf times;
	times.actime = static_cast<time_t>(last_modified);
	times.modtime = static_cast<time_t>(last_modified);
    int rc = utime(ConvertToUTF8(fn).c_str(), &times);
	return rc==0;
}

#ifndef OS_FUNC_NO_SERVER
bool copy_file(const std::wstring &src, const std::wstring &dst)
{
    IFile *fsrc=Server->openFile(src, MODE_READ);
	if(fsrc==NULL) return false;
    IFile *fdst=Server->openFile(dst, MODE_WRITE);
	if(fdst==NULL)
	{
        Server->destroy(fsrc);
		return false;
	}
	char buf[4096];
	size_t rc;
	bool has_error=false;
	while( (rc=(_u32)fsrc->Read(buf, 4096, &has_error))>0)
	{
		if(rc>0)
		{
			fdst->Write(buf, (_u32)rc, &has_error);

			if(has_error)
			{
				break;
			}
		}
	}

    Server->destroy(fsrc);
    Server->destroy(fdst);

	if(has_error)
	{
		return false;
	}
	else
	{
		return true;
	}
}
#endif //OS_FUNC_NO_SERVER

SFile getFileMetadataWin( const std::wstring &path, bool with_usn)
{
	return getFileMetadata(path);
}

SFile getFileMetadata( const std::wstring &path )
{
	SFile ret;
	ret.name=path;

	struct stat64 f_info;
    int rc=lstat64(ConvertToUTF8(path).c_str(), &f_info);

	if(rc==0)
	{
		if(S_ISDIR(f_info.st_mode) )
		{
			ret.isdir = true;
		}

		ret.size = f_info.st_size;
		ret.last_modified = f_info.st_mtime;
		ret.created = f_info.st_ctime;

		return ret;
	}
	else
	{
		return SFile();
	}
}

std::wstring os_get_final_path(std::wstring path)
{
    char* retptr = realpath(ConvertToUTF8(path).c_str(), NULL);
	if(retptr==NULL)
	{
		return std::wstring();
	}
    std::wstring ret = ConvertToUnicode(retptr);
	free(retptr);
	return ret;
}

bool os_path_absolute(const std::wstring& path)
{
    if(!path.empty() && path[0]=='/')
    {
        return true;
    }
    else
    {
        return false;
    }
}

int os_popen(const std::string& cmd, std::string& ret)
{
	ret.clear();

	FILE* in = NULL;

#ifndef _WIN32
#define _popen popen
#define _pclose pclose
#endif

	in = _popen(cmd.c_str(), "r");

	if(in==NULL)
	{
		return -1;
	}

	char buf[4096];
	size_t read;
	do
	{
		read=fread(buf, 1, sizeof(buf), in);
		if(read>0)
		{
			ret.append(buf, buf+read);
		}
	}
	while(read==sizeof(buf));

	return _pclose(in);
}

