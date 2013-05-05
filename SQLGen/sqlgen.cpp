#include "sqlgen.h"
#include "../stringtools.h"
#include <regex>
#include <iostream>

enum CPPFileTokenType
{
	CPPFileTokenType_Code,
	CPPFileTokenType_Comment
};

struct CPPToken
{
	CPPToken(const std::string& data, CPPFileTokenType type)
		: data(data), type(type)
	{
	}

	std::string data;
	CPPFileTokenType type;
};

enum TokenizeState
{
	TokenizeState_None,
	TokenizeState_CommentMultiline,
	TokenizeState_CommentSingleline
};

std::vector<CPPToken> tokenizeFile(std::string &cppfile)
{
	std::regex find_comments("(/\\*(\\S|\\s)*?\\*/)|(//.*)", std::regex::ECMAScript);
	auto comments_begin=std::regex_iterator<std::string::iterator>(cppfile.begin(), cppfile.end(), find_comments);
	auto comments_end=std::regex_iterator<std::string::iterator>();

	std::vector<CPPToken> tokens;
	size_t lastPos=0;
	for(auto i=comments_begin;i!=comments_end;++i)
	{
		auto m=*i;
		size_t pos=m.position(0);
		if(lastPos<pos)
		{
			tokens.push_back(CPPToken(cppfile.substr(lastPos, pos-lastPos), CPPFileTokenType_Code));
			lastPos=pos;
		}
		tokens.push_back(CPPToken(m.str(), CPPFileTokenType_Comment));
		lastPos+=m.length();
	}
	if(lastPos<cppfile.size())
	{
		tokens.push_back(CPPToken(cppfile.substr(lastPos), CPPFileTokenType_Code));
	}

	return tokens;
}

struct AnnotatedCode
{
	AnnotatedCode(std::map<std::string, std::string> annotations, std::string code)
		: annotations(annotations), code(code)
	{
	}

	AnnotatedCode(std::string code)
		: code(code)
	{
	}

	std::map<std::string, std::string> annotations;
	std::string code;
};

std::string cleanup_annotation(const std::string& annotation)
{
	int state=0;
	std::string ret;
	for(size_t i=0;i<annotation.size();++i)
	{
		if(state==0)
		{
			if(annotation[i]=='\n' || annotation[i]=='\r')
			{
				state=1;
			}
		}
		else if(state==1)
		{
			if(annotation[i]!=' ' && annotation[i]!='*' && annotation[i]!='\n' )
			{
				state=0;
			}
		}

		if(annotation[i]=='\n')
		{
			ret+=" ";
		}

		if(state==0)
		{
			ret+=annotation[i];
		}
	}

	return ret;
};

std::string extractFirstFunction(const std::string& data)
{
	int c=0;
	bool was_in_function=false;
	for(size_t i=0;i<data.size();++i)
	{
		if(data[i]=='{') ++c;
		if(data[i]=='}') --c;

		if(c>0)
		{
			was_in_function=true;
		}

		if(c==0 && was_in_function)
		{
			return data.substr(0, i);
		}
	}

	return std::string();
}

std::vector<AnnotatedCode> getAnnotatedCode(const std::vector<CPPToken>& tokens)
{
	std::vector<AnnotatedCode> ret;
	for(size_t i=0;i<tokens.size();++i)
	{
		if(tokens[i].type==CPPFileTokenType_Comment)
		{
			std::map<std::string, std::string> annotations;

			std::regex find_annotations("@([^ \\r\\n]*)[ ]*((\\S|\\s)*?)(?=(\\*/)|@)", std::regex::ECMAScript);

			for(auto it=std::regex_iterator<std::string::const_iterator>(tokens[i].data.begin(), tokens[i].data.end(), find_annotations);
				it!=std::regex_iterator<std::string::const_iterator>();++it)
			{
				auto m=*it;
				std::string annotation_text=m[2].str();
				annotations[m[1].str()]=trim(cleanup_annotation(annotation_text));
			}

			ret.push_back(AnnotatedCode(tokens[i].data));

			if(!annotations.empty())
			{
				if(i+1<tokens.size() && tokens[i+1].type==CPPFileTokenType_Code)
				{
					std::string next_code=tokens[i+1].data;
					std::string first_function=extractFirstFunction(next_code);

					if(!first_function.empty())
					{
						ret.push_back(AnnotatedCode(annotations, first_function));
						ret.push_back(AnnotatedCode(next_code.substr(first_function.size())));
					}
					else
					{
						ret.push_back(AnnotatedCode(annotations, ""));
					}
				}
			}
		}
		else
		{
			ret.push_back(AnnotatedCode(tokens[i].data));
		}
	}

	return ret;
}

struct ReturnType
{
	ReturnType(std::string type, std::string name)
		: type(type), name(name)
	{
	}

	std::string type;
	std::string name;
};

std::vector<ReturnType> parseReturnTypes(std::string return_str)
{
	std::vector<std::string> toks;
	Tokenize(return_str, toks, ",");
	std::vector<ReturnType> ret;
	for(size_t i=0;i<toks.size();++i)
	{
		toks[i]=trim(toks[i]);
		ret.push_back(ReturnType(getuntil(" ", toks[i]), getafter(" ", toks[i])));
	}
	return ret;
}

std::string parseSqlString(std::string sql, std::vector<ReturnType>& types)
{
	std::regex find_var(":([^ (])*(\\([^)]*?\\))",std::regex::ECMAScript);
	size_t lastPos=0;
	std::string retSql;
	for(auto it=std::regex_iterator<std::string::const_iterator>(sql.begin(), sql.end(), find_var);
				it!=std::regex_iterator<std::string::const_iterator>();++it)
	{
		auto m=*it;
		if(m.position()>lastPos)
		{
			retSql+=sql.substr(lastPos, m.position()-lastPos);
			lastPos=m.position();
		}
		retSql+="?";
		types.push_back(ReturnType(m[2].str(), m[1].str()));
	}
	if(lastPos<sql.size())
	{
		retSql+=sql.substr(lastPos);
	}
	return retSql;
}

struct GeneratedData
{
	std::string createQueriesCode;
	std::string destroyQueriesCode;
	std::string structures;
};

void generateStructure(std::string name, std::vector<ReturnType> return_types, GeneratedData& gen_data)
{
	gen_data.structures+="\tstruct "+name+"\r\n";
	gen_data.structures+="\t{\r\n";
	for(size_t i=0;i<return_types.size();++i)
	{
		std::string type=return_types[i].type;
		if(type=="string")
			type="std::string";

		gen_data.structures+="\t\t"+type+" "+return_types[i].name+";\r\n";
	}
	gen_data.structures+="\t}\r\n";
}

AnnotatedCode generateSqlFunction(IDatabase* db, AnnotatedCode input, GeneratedData& gen_data)
{
	std::string sql=input.annotations["sql"];
	std::string func=input.annotations["func"];
	std::string return_type=getuntil(" ", func);
	std::string funcsig=getafter(" ", func);

	std::string struct_name=return_type;

	std::string query_name=funcsig;
	if(query_name.find("::")!=std::string::npos)
	{
		query_name=getafter("::", query_name);
	}

	bool return_vector=false;

	if(return_type.find("std::vector")==0)
	{
		struct_name=getbetween("<", ">", return_type);
		return_vector=true;
	}

	bool select_statement=false;
	if(strlower(sql).find("select"))
	{
		select_statement=true;
	}

	std::string return_vals=input.annotations["return"];

	std::vector<ReturnType> return_types=parseReturnTypes(return_vals);

	std::vector<ReturnType> params;
	std::string parsedSql=parseSqlString(sql, params);

	IQuery *q=db->Prepare("EXPLAIN "+parsedSql, true);

	if(q==NULL)
	{
		std::cout << "ERROR preparing statement: " << parsedSql << std::endl;
		return;
	}

	generateStructure(struct_name, return_types, gen_data);

	std::string code=return_type+" "+funcsig+"(";
	for(size_t i=0;i<params.size();++i)
	{
		if(i>0)
		{
			code+=" ,";
		}
		std::string type=params[i].type;
		if(type=="string")
		{
			type="std::string";
		}
		code+=type+" "+params[i].name;
	}
	if(params.empty())
	{
		code+="void";
	}
	code+=")\r\n{\r\n";

	gen_data.createQueriesCode+="\t"+query_name+"="+"db->Prepare(\""+parsedSql+"\", false);\r\n";
	gen_data.destroyQueriesCode+="\tdb->destroyQuery("+query_name+");\r\n";

	for(size_t i=0;i<params.size();++i)
	{
		code+="\t"+query_name+"->Bind("+params[i].name+");\r\n";
	}

	if(select_statement)
	{
		code+="\tdb_results res="+query_name+"->Read();\r\n";
	}

	if(!params.empty())
	{
		code+="\t"+query_name+"->Reset();\r\n";
	}

	if(return_vector)
	{
		code+="\tstd::vector<"+struct_name+"> ret;\r\n";
		code+="\tfor(size_t i=0;i<res.size();++i)\r\n";
		code+="\t{\r\n";
		for(size_t i=0;i<return_types.size();++i)
		{
			code+="\t\t"+struct_name+" tmp;\r\n";
			if(return_types[i].type=="int")
			{
				code+="\t\ttmp."+return_types[i].name+"=watoi(res[i][L\""+return_types[i].name+"\"]);\r\n";
			}
			else
			{
				code+="\t\ttmp."+return_types[i].name+"=res[i][L\""+return_types[i].name+"\"];\r\n";
			}			
		}
		code+="\t}\r\n";
		code+="\treturn ret;\r\n";
	}
	return AnnotatedCode(input.annotations, code);
}

void generateCode(IDatabase* db, std::vector<AnnotatedCode> annotated_code)
{
	for(size_t i=0;i<annotated_code.size();++i)
	{
		AnnotatedCode& curr=annotated_code[i];
		if(!curr.annotations.empty())
		{
			if(curr.annotations.find("-SQLGenAccess")!=curr.annotations.end())
			{
				annotated_code[i]=generateSqlFunction(db, curr);
			}
		}
	}
}

void sqlgen(IDatabase* db, std::string &cppfile, std::string &headerfile)
{
	std::vector<CPPToken> tokens=tokenizeFile(cppfile);
	std::vector<AnnotatedCode> annotated_code=getAnnotatedCode(tokens);

}