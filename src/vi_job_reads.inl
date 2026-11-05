// Counter observations are scoped to this native job's finite execution window.
// They prove returned polling state, never a gameplay action or input causality.
vi_input_reads::Sample JobReadSample(int vk){
 const auto& t=g_keyTrace[vk];vi_input_reads::Sample out;
 const ChannelTrace* channels[]={&t.gaks,&t.gks,&t.gkbs,&t.di_state,&t.di_buffer,&t.raw_data,&t.raw_buffer};
 for(unsigned i=0;i<vi_input_reads::channels;++i)if(!channels[i]->snapshot(out.reads[i],out.down[i]))out.valid=false;
 out.messages=t.delivered_down.load();return out;
}
std::array<int,2> JobReadAliases(int vk){
 const int families[][3]={{VK_SHIFT,VK_LSHIFT,VK_RSHIFT},{VK_CONTROL,VK_LCONTROL,VK_RCONTROL},{VK_MENU,VK_LMENU,VK_RMENU}};
 for(const auto& family:families)if(vk==family[0]||vk==family[1]||vk==family[2]){std::array<int,2> aliases{};unsigned n=0;for(int member:family)if(member!=vk)aliases[n++]=member;return aliases;}
 return {};
}
void JobReadsStart(InputJob& job,ULONGLONG now){
 auto& observation=job.readObservation;observation.started=true;observation.pad=job.pad;observation.began=now;
 if(!job.pad&&job.kind!=InputJob::Placement)for(int key:job.keys)if(key>0&&key<256&&std::none_of(observation.keys.begin(),observation.keys.end(),[&](const vi_input_reads::Key& old){return old.vk==key;})){
  vi_input_reads::Key entry;entry.vk=key;entry.before=JobReadSample(key);const auto aliases=JobReadAliases(key);
  for(unsigned n=0;n<aliases.size();++n)if(aliases[n]){entry.aliases[n].vk=aliases[n];entry.aliases[n].before=JobReadSample(aliases[n]);}
  observation.keys.push_back(entry);
 }
}
void JobReadsSample(InputJob& job,bool freeze=false){
 auto& observation=job.readObservation;if(!observation.started||observation.frozen)return;
 if(job.pad){std::lock_guard<std::mutex> pad(g_padStateMx);if(g_padReadOwner==job.id){observation.padReturns=g_padReadReturns;observation.valid=observation.valid&&g_padReadValid;}}
 else for(auto& key:observation.keys){key.delta=vi_input_reads::Difference(key.before,JobReadSample(key.vk));if(!key.delta.valid)observation.valid=false;
  for(auto& alias:key.aliases)if(alias.vk)alias.delta=vi_input_reads::Difference(alias.before,JobReadSample(alias.vk));}
 if(freeze){observation.ended=HousekeepingNow();observation.frozen=true;}
}
void JobReadFields(JOut& out,const InputJob& job){
 const auto& observation=job.readObservation;JOut reads;reads.num("version",1);reads.bol("started",observation.started);reads.bol("terminal",job.done);reads.bol("frozen",observation.frozen);
 reads.str("kind",job.pad?"pad":"keys");reads.raw("application_response","null");
 reads.num("window_ms",observation.started?(double)((observation.frozen?observation.ended:HousekeepingNow())-observation.began):0);
 bool valid=observation.valid;std::uint64_t padReturns=observation.padReturns;if(job.pad&&!observation.frozen){std::lock_guard<std::mutex> pad(g_padStateMx);if(g_padReadOwner==job.id){padReturns=g_padReadReturns;valid=valid&&g_padReadValid;}}unsigned observed=0;std::string keys="[";
 if(!job.pad)for(const auto& key:observation.keys){const auto delta=observation.frozen?key.delta:vi_input_reads::Difference(key.before,JobReadSample(key.vk));std::array<vi_input_reads::Delta,2> aliases{};
  for(unsigned n=0;n<aliases.size();++n)if(key.aliases[n].vk)aliases[n]=observation.frozen?key.aliases[n].delta:vi_input_reads::Difference(key.aliases[n].before,JobReadSample(key.aliases[n].vk));
  const auto evidence=vi_input_reads::KeyEvidence(delta,aliases);valid=valid&&evidence.measured;
  if(delta.valid&&delta.down)++observed;if(keys.size()>1)keys+=",";JOut item;item.num("vk",key.vk);item.bol("measured",evidence.measured);item.str("poll_reads",std::to_string(delta.reads));item.str("down_returns",std::to_string(delta.down));item.str("message_dispatches",std::to_string(delta.messages));
  if(key.aliases[0].vk){item.bol("exact_vk_measured",delta.valid);item.bol("modifier_alias_ambiguous",evidence.aliasAmbiguous);std::string related="[";
   for(unsigned n=0;n<aliases.size();++n){if(n)related+=",";JOut alias;alias.num("vk",key.aliases[n].vk);alias.bol("measured",aliases[n].valid);alias.str("poll_reads",std::to_string(aliases[n].reads));alias.str("down_returns",std::to_string(aliases[n].down));related+=alias.done();}item.raw("modifier_aliases",related+"]");}
  keys+=item.done();}
 keys+="]";reads.raw("keys",keys);reads.str("pad_returns",std::to_string(padReturns));
 const bool applicable=job.pad||!observation.keys.empty();const bool measured=observation.started&&valid&&applicable;
 reads.bol("measured",measured);reads.bol("applicable",applicable);
 if(measured)reads.bol("all_requested_observed",job.pad?padReturns>0:observed==observation.keys.size());else reads.raw("all_requested_observed","null");
 reads.num("keys_observed",observed);reads.num("keys_requested",(double)observation.keys.size());
 reads.str("outcome",!observation.started?"pending":!applicable?"not_applicable":!valid?"unavailable":!job.done?"pending":(job.pad?padReturns>0:observed==observation.keys.size())?"observed":observed?"partial":"not_observed");
 reads.str("scope","polling high-state returns between native job start and cleanup; related modifier alias highs without an exact high are ambiguous, not exact-VK consumption or absence; pad returns match this job's publication owner; messages are reported separately; counter evidence does not establish causality or gameplay response; no input is retried");out.raw("input_read_observation",reads.done());
}
