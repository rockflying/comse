#include "search_engine.h"
#include "limonp/Md5.hpp"
#include <algorithm>
#include <fstream> 
#include "easy_log.h"
#include <sys/time.h>

extern cppjieba::Jieba g_jieba;
extern pthread_mutex_t g_jieba_lock;
extern easy_log g_log;

int policy_compute_score(std::string &query,std::vector<std::string> & term_list,Json::Value & query_json,info_storage & one_info,int search_mode)
{
	int ret = DEFAULT_SCORE;
	if (one_info.info_json_ori_str.size() <= 0 || 
			one_info.title4se.size() <= 0 ) {return ret;}

	if (search_mode == or_mode)
	{
		int or_terms_length = 0;
		int or_terms_num = 0;

		if ( one_info.term4se_set.size() > 0)
		{
			std::set<std::string>::iterator term_hash_it;

			for (int i = 0 ; i < term_list.size();i++)
			{
				term_hash_it = one_info.term4se_set.find(term_list[i]);
				if (term_hash_it != one_info.term4se_set.end()) 
				{
					or_terms_length += term_list[i].size();
					or_terms_num++;
				}
			}
		}

		ret = (or_terms_num * 1000 + or_terms_length);
	}
	else
	{
		ret = (10000 - one_info.title4se.size());
	}
	return ret;
}

void policy_cut_query(cppjieba::Jieba &jieba,std::string & query,std::vector<std::string> &term_list)
{
	if (query.size() <= 0) {return;}
	std::vector<cppjieba::Word> jiebawords;
	{//enter auto mutex lock
		AutoLock_Mutex auto_lock0(&g_jieba_lock);
		jieba.CutForSearch(query, jiebawords, true);
	}

	//clear
	term_list.clear();
	for (int i = 0;i < jiebawords.size();i++)
	{
		term_list.push_back(jiebawords[i].word);
	}
}

bool json_2_info(Json::Value & json,std::string & json_show_str,info_storage & info_stg)
{
	//check in policy
	info_stg.title4se = json["cmd_info"]["title4se"].asString();
	for (int i = 0;i < json["cmd_info"]["term4se"].size();i++)
	{
		info_stg.term4se_set.insert(json["cmd_info"]["term4se"][i].asString());
	}
	info_stg.info_json_ori_str = json_show_str;

	return true;
}

std::string str_escape(std::string  str)
{
	std::string ret_str;
	for (int i = 0 ;i < str.size();i++)
	{
		if ( str.c_str()[i] == '\r')
		{
			ret_str += '\\';
			ret_str += 'r';
		}
		else if (  str.c_str()[i] == '\n')
		{
			ret_str += '\\';
			ret_str += 'n';
		}
		else
		{
			ret_str += str.c_str()[i];
		}
	}
	return ret_str;
}

bool info_2_json(info_storage & info_stg, std::string & json_output_str)
{
	json_output_str.clear();
	json_output_str += "{\"cmd_info\":{\"term4se\":[";
	for (std::set<std::string>::iterator it = info_stg.term4se_set.begin(); it != info_stg.term4se_set.end() ;it++)
	{
		if (it == info_stg.term4se_set.begin())
		{json_output_str += "\"" + str_escape(*it) + "\"";}
		else
		{json_output_str += ",\"" + str_escape(*it) + "\"";}
	}
	json_output_str += "],\"title4se\":\"";
	json_output_str += str_escape(info_stg.title4se);
	json_output_str += "\"},\"cmd_type\":\"add\",\"show_info\":";
	//delete last \n
	if (info_stg.info_json_ori_str.size() > 0 
			&& info_stg.info_json_ori_str.c_str()[info_stg.info_json_ori_str.size()-1] == '\n')
		json_output_str += info_stg.info_json_ori_str.substr(0,info_stg.info_json_ori_str.size()-1);
	else
		json_output_str += info_stg.info_json_ori_str;
	json_output_str += "}";
	return true;
}

////////////////////////////////////////////////////////////////////

//tools:

class sort_myclass {
	public:
		sort_myclass(uint32_t a, int b):pos(a), score(b){}
		sort_myclass(){}
		~sort_myclass(){}
		uint32_t pos;
		int score;

		bool operator < (const sort_myclass &m)const {
			return score > m.score;
		}
};

void merge_sort_2way(std::vector<uint32_t> & in_way1,std::vector<uint32_t> & in_way2,std::vector<uint32_t> & out_way)
{
	out_way.clear();
	for (int i = 0,j =0;;)
	{
		if (i >= in_way1.size() && j >= in_way2.size())
		{
			break;
		}
		else if (i >= in_way1.size())
		{//in_way1 is empty,push in_way2
			for (;j<in_way2.size();j++) 
			{
				if (out_way.size() <= 0 || in_way2[j] != out_way.back())
				{
					out_way.push_back(in_way2[j]);
					if (out_way.size() > MAX_RECALL_NUM) //avoid too many search and overflow stack size
					{
						return;
					}
				}
			}
			break;
		}
		else if (j >= in_way2.size())
		{//in_way2 is empty,push in_way1
			for (;i<in_way1.size();i++) 
			{
				if (out_way.size() <= 0 || in_way1[i] != out_way.back())
				{
					out_way.push_back(in_way1[i]);
					if (out_way.size() > MAX_RECALL_NUM) //avoid too many search and overflow stack size
					{
						return;
					}
				}
			}
			break;
		}

		if (in_way1[i] < in_way2[j])
		{
			if (out_way.size() <= 0 ||
					in_way1[i] != out_way.back())
			{
				out_way.push_back(in_way1[i]);
				if (out_way.size() > MAX_RECALL_NUM) //avoid too many search and overflow stack size
				{
					return;
				}
			}
			i++;
		}
		else
		{
			if (out_way.size() <= 0 ||
					in_way2[j] != out_way.back())
			{
				out_way.push_back(in_way2[j]);
				if (out_way.size() > MAX_RECALL_NUM) //avoid too many search and overflow stack size
				{
					return;
				}
			}
			j++;
		}

	}

}

////////////////////////////////////////////////////////////////////

bool Search_Engine::add(std::vector<std::string> & term_list,Json::Value & one_info)
{
	bool ret = false;
	char log_buff[1024] = {0};
	//check if validate
	if (term_list.size() <= 0 || one_info.empty()) 
	{
		snprintf(log_buff,sizeof(log_buff),"term_list is empty or one_info is empty");
		g_log.write_record(log_buff);
		return false;
	}
	//jisuan md5
	std::string json_str = json_writer.write(one_info["show_info"]);
	std::string md5_str;
	uint32_t index_num = 0;

	limonp::md5String(json_str.c_str(),md5_str);
	//add info_md5
	{// enter lock
		AUTO_LOCK auto_lock(&_info_md5_dict_lock,true);
#ifdef _USE_HASH_
		std::tr1::unordered_map<std::string,uint32_t>::iterator it = _info_md5_dict.find(md5_str);
#else
		std::map<std::string,uint32_t>::iterator it = _info_md5_dict.find(md5_str);
#endif
		//check if add
		if (it != _info_md5_dict.end())
		{
			snprintf(log_buff,sizeof(log_buff),"add duplicate data:%s",json_str.c_str());
			g_log.write_record(log_buff);
			return true;		
		}
		else
		{
			index_num = ++max_index_num;
			_info_md5_dict.insert ( std::pair<std::string,uint32_t>(md5_str,index_num) );
		}
	}
	//output log
	if (index_num % 10000 == 0)
	{
		snprintf(log_buff,sizeof(log_buff),"now max_index_num:%d",index_num);
		g_log.write_record(log_buff);
	}

	//load info
	info_storage info_stg;
	json_2_info(one_info,json_str,info_stg);

	//add info
	{// enter lock 
		AUTO_LOCK auto_lock(&_info_dict_lock,true);
		_info_dict.insert ( std::pair<uint32_t,info_storage>(index_num,info_stg) );
	}
	//add index
	{
		ret = false;
		for (int i = 0 ;i < term_list.size(); i++)
		{
			if( _index_core.insert_index(term_list[i],index_num))
			{ret = true;}

			//check if need shrink
			index_hash_value i_hash_value = _index_core.find_index(term_list[i]);
			if (i_hash_value.sum_node_num >= DEFAULT_ADD_NEED_SHRINK_NODE &&
					(i_hash_value.use_data_num / i_hash_value.sum_node_num) < DEFAULT_ADD_NEED_SHRINK_AVG) 
			{
				snprintf(log_buff,sizeof(log_buff),"add do shrink index[sum_node:%d] [use_data:%d] [del_data:%d] [term:%s]"
						,i_hash_value.sum_node_num,i_hash_value.use_data_num,i_hash_value.del_data_num
						,term_list[i].c_str());
				g_log.write_record(log_buff);

				_index_core.shrink_index(term_list[i]);
				i_hash_value = _index_core.find_index(term_list[i]);

				snprintf(log_buff,sizeof(log_buff),"add done shrink index[sum_node:%d] [use_data:%d] [del_data:%d] [term:%s]"
						,i_hash_value.sum_node_num,i_hash_value.use_data_num,i_hash_value.del_data_num
						,term_list[i].c_str());
				g_log.write_record(log_buff);
			}
		}
	}
	return ret;
}

bool Search_Engine::del(std::vector<std::string> & term_list,Json::Value & one_info)
{
	bool ret = false;
	uint32_t index_num = 0;
	//check if validate
	if (term_list.size() <= 0 || one_info.empty()) {return false;}
	//jisuan md5
	std::string json_str = json_writer.write(one_info["show_info"]);
	std::string md5_str;
	char log_buff[1024] = {0};
	limonp::md5String(json_str.c_str(),md5_str);
	//add info_md5
	{// enter lock
		AUTO_LOCK auto_lock(&_info_md5_dict_lock,true);
#ifdef _USE_HASH_
		std::tr1::unordered_map<std::string,uint32_t>::iterator it = _info_md5_dict.find(md5_str);
#else
		std::map<std::string,uint32_t>::iterator it = _info_md5_dict.find(md5_str);
#endif
		//check if del
		if (it != _info_md5_dict.end())
		{
			index_num = it->second;
			_info_md5_dict.erase(it);
		}
		else
		{
			return false;
		}
	}
	//del info
	{// enter lock 
		AUTO_LOCK auto_lock(&_info_dict_lock,true);
		_info_dict.erase (index_num);
	}
	//del index
	{
		ret = false;
		for (int i = 0 ;i < term_list.size(); i++)
		{
			if( _index_core.delete_index(term_list[i],index_num))
			{ret = true;}

			//check if need shrink
			index_hash_value i_hash_value = _index_core.find_index(term_list[i]);
			if (i_hash_value.del_data_num >= DEFAULT_DEL_NEED_SHRINK) 
			{
				snprintf(log_buff,sizeof(log_buff),"del do shrink index[sum_node:%d] [use_data:%d] [del_data:%d] [term:%s]"
						,i_hash_value.sum_node_num,i_hash_value.use_data_num,i_hash_value.del_data_num
						,term_list[i].c_str());
				g_log.write_record(log_buff);

				_index_core.shrink_index(term_list[i]);
				i_hash_value = _index_core.find_index(term_list[i]);

				snprintf(log_buff,sizeof(log_buff),"del done shrink index[sum_node:%d] [use_data:%d] [del_data:%d] [term:%s]"
						,i_hash_value.sum_node_num,i_hash_value.use_data_num,i_hash_value.del_data_num
						,term_list[i].c_str());
				g_log.write_record(log_buff);
			}
		}
	}
	return ret;
}

bool Search_Engine::search(std::vector<std::string> & in_term_list,
		std::string & in_query,
		Json::Value & query_json,
		std::vector<std::string> & out_info_vec,
		std::vector<int> & out_score_vec,
		int &recall_num,
		int in_start_id,int in_ret_num,int in_max_ret_num,int search_mode)
{
	//check
	if (in_term_list.size() <= 0 ||
			in_query.size() <= 0 ||
			query_json.empty() ||
			in_start_id < 0 ||
			in_ret_num <= 0 ||
			in_max_ret_num <= 0 )
	{return false;}

	//get time
	char log_buff[1024] = {0};
	struct timeval l_time[6] = {0};
	int l_time_pos = 0;

	gettimeofday(&l_time[l_time_pos++],0);
	//select a term which has min index numbers
	int term_pos = 0;
	int min_index_numbers = 0;
	if (search_mode == and_mode)
	{
		for (int i = 0;i < in_term_list.size(); i++)
		{
			index_hash_value i_hash_value = _index_core.find_index(in_term_list[i]);

			if (i_hash_value.sum_node_num <= 0) 
			{//this term not exist index
				return true;
			}

			if (i == 0)
			{//init
				term_pos = i;
				min_index_numbers = i_hash_value.use_data_num;
			}
			else
			{
				if (i_hash_value.use_data_num < min_index_numbers)
				{
					term_pos = i;
					min_index_numbers = i_hash_value.use_data_num;
				}
			}
		}
	}
	//get recall index numbers vector
	std::vector<uint32_t> query_in;
	std::vector<uint32_t> query_out;
	query_in.reserve(MAX_RECALL_NUM + 128);
	query_out.reserve(MAX_RECALL_NUM + 128);

	std::vector<uint32_t> & query_in_it = query_in;
	std::vector<uint32_t> & query_out_it = query_out;

	_index_core.all_query_index(in_term_list[term_pos],query_in_it);

	if (search_mode == and_mode)
	{
		for (int i = 0;i < in_term_list.size(); i++)
		{
			if (i == term_pos) {continue;}
			_index_core.cross_query_index(in_term_list[i],query_in_it,query_out_it);
			//swap query_in and query_out ,and clear query_out to next step use
			std::vector<uint32_t> & tmp_it = query_in_it;
			query_in_it = query_out_it;
			query_out_it = tmp_it;
			query_out_it.clear();
		}
	}
	else
	{
		for (int i = 0;i < in_term_list.size(); i++)
		{
			std::vector<uint32_t>  tmp_vec;
			if (i == term_pos) {continue;}

			_index_core.all_query_index(in_term_list[i],tmp_vec);
			merge_sort_2way(query_in_it,tmp_vec,query_out_it);
			//swap query_in and query_out ,and clear query_out to next step use
			std::vector<uint32_t> & tmp_it = query_in_it;
			query_in_it = query_out_it;
			query_out_it = tmp_it;
			query_out_it.clear();

			
			if (query_in_it.size() > MAX_RECALL_NUM) //avoid too many search and overflow stack size
			{
				break;
			}

		}

	}

	gettimeofday(&l_time[l_time_pos++],0);

	//check in_start_id and in_ret_num
	if (in_start_id >= query_in_it.size())
	{return false;}
	if (in_ret_num >= in_max_ret_num)
	{in_ret_num = in_max_ret_num;}

	//compute every index score,and get the need number vector,can use min heap sort
	std::vector< sort_myclass > vect_score;
	vect_score.reserve(MAX_RECALL_NUM + 128);

	{
		AUTO_LOCK auto_lock(&_info_dict_lock,false);

		for (int i = 0; i < query_in_it.size(); i++)
		{
#ifdef _USE_HASH_
			std::tr1::unordered_map<uint32_t,info_storage>::iterator it = _info_dict.find(query_in_it[i]);
#else
			std::map<uint32_t,info_storage>::iterator it = _info_dict.find(query_in_it[i]);
#endif
			if (it != _info_dict.end())
			{
				int score = policy_compute_score(in_query,in_term_list,query_json,it->second,search_mode);
				sort_myclass my(query_in_it[i], score);
				vect_score.push_back(my);
			}
			else
			{
				//not find,default score is 0.0
				sort_myclass my(query_in_it[i], DEFAULT_SCORE);
				vect_score.push_back(my);
			}

		}
	}

	gettimeofday(&l_time[l_time_pos++],0);
	//sort
	std::sort(vect_score.begin(), vect_score.end()); 

	gettimeofday(&l_time[l_time_pos++],0);
	//out to out_vec
	{
		AUTO_LOCK auto_lock(&_info_dict_lock,false);

		for (int i = in_start_id;i < in_start_id + in_ret_num && i < vect_score.size();i++)
		{
#ifdef _USE_HASH_
			std::tr1::unordered_map<uint32_t,info_storage>::iterator it = _info_dict.find(vect_score[i].pos);
#else
			std::map<uint32_t,info_storage>::iterator it = _info_dict.find(vect_score[i].pos);
#endif
			if (it != _info_dict.end())
			{
				out_info_vec.push_back(it->second.info_json_ori_str);
				//dump score to json
				out_score_vec.push_back(vect_score[i].score);
			}
		}

		//output recall num
		recall_num = query_in_it.size(); 

	}
	gettimeofday(&l_time[l_time_pos++],0);

	snprintf(log_buff,sizeof(log_buff),"cal_time recall=%d|compute=%d|sort=%d|package=%d search_mode=%d:recall_num=%d:query=%s"
			,int((l_time[1].tv_sec - l_time[0].tv_sec)*1000000) + int(l_time[1].tv_usec - l_time[0].tv_usec)  //recall
			,int((l_time[2].tv_sec - l_time[1].tv_sec)*1000000) + int(l_time[2].tv_usec - l_time[1].tv_usec)  //compute
			,int((l_time[3].tv_sec - l_time[2].tv_sec)*1000000) + int(l_time[3].tv_usec - l_time[2].tv_usec)  //sort
			,int((l_time[4].tv_sec - l_time[3].tv_sec)*1000000) + int(l_time[4].tv_usec - l_time[3].tv_usec)  //package
			,search_mode,recall_num,in_query.c_str()
		);
	g_log.write_record(log_buff);
	return true;
}

bool Search_Engine::dump_to_file()
{
	std::ofstream os;  
	os.open(_dump_file.c_str());

	if (!os.is_open())
	{
		return false;
	}
	else
	{
		{
			AUTO_LOCK auto_lock(&_info_dict_lock,false);
#ifdef _USE_HASH_
			for (std::tr1::unordered_map<uint32_t,info_storage>::iterator it = _info_dict.begin(); it != _info_dict.end(); ++it)
#else
				for (std::map<uint32_t,info_storage>::iterator it = _info_dict.begin(); it != _info_dict.end(); ++it)
#endif
				{
					std::string output_str;
					info_2_json(it->second,output_str);
					os << output_str << "\n";  
				}
		}
		os.close();  
		return true;
	}
}

bool Search_Engine::load_from_file()
{
	std::ifstream is;  
	is.open(_load_file.c_str());
	char in_buff[MAX_GETLINE_BUFF] = {0};

	if (!is.is_open())
	{
		return false;
	}

	while (!is.eof() )  
	{  
		is.getline(in_buff,MAX_GETLINE_BUFF);
		Json::Value tmp_json;

		//parse use json
		if (!json_reader.parse(in_buff,(char *)in_buff + strlen(in_buff), tmp_json)) 
		{
			g_log.write_record("json parse error");
			g_log.write_record(in_buff);
			continue;
		}

		//parse json ok 
		bool is_term_list_in = false;
		std::vector<std::string> term_list;

		if ( tmp_json["cmd_type"].isNull()
				|| tmp_json["cmd_info"].isNull() 
				|| tmp_json["show_info"].isNull() 
				|| tmp_json["cmd_info"]["title4se"].isNull() )
		{
			g_log.write_record("json check error");
			g_log.write_record(in_buff);
			continue;
		}

		if (!tmp_json["cmd_info"]["term4se"].isNull() 
				&& tmp_json["cmd_info"]["term4se"].isArray()
				&& tmp_json["cmd_info"]["term4se"].size() > 0)
		{
			is_term_list_in = true;
			for (unsigned int i = 0; i < tmp_json["cmd_info"]["term4se"].size(); i++)
			{
				term_list.push_back(tmp_json["cmd_info"]["term4se"][i].asString());
			}
		}

		//parse query
		std::string query = tmp_json["cmd_info"]["title4se"].asString();

		//parse cmd_type
		if (tmp_json["cmd_type"].asString() == "add")
		{
			if (!is_term_list_in)
			{//get term_list
				policy_cut_query(g_jieba,query,term_list);
				//add term in json_in
				Json::Value term_list_json;
				for (int i = 0;i < term_list.size();i++)
				{term_list_json.append(term_list[i]);}

				tmp_json["cmd_info"]["term4se"] = term_list_json;
			}

			if (add(term_list,tmp_json))
			{
				//return true
			}
			else
			{
				//return false
			}

		}
		else
		{
			g_log.write_record("comse cmd_type error");
			g_log.write_record(in_buff);
			continue;
		}
	}

	return true;
}
