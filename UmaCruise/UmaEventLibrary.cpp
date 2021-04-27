#include "stdafx.h"
#include "UmaEventLibrary.h"

#include <regex>
#include <boost\algorithm\string\trim.hpp>
#include <boost\algorithm\string\replace.hpp>
#include <boost\filesystem.hpp>
#include <boost\optional.hpp>

#include "Utility\CodeConvert.h"
#include "Utility\CommonUtility.h"
#include "Utility\json.hpp"
#include "Utility\Logger.h"


using json = nlohmann::json;
using namespace CodeConvert;


boost::optional<std::wstring> retrieve(
	simstring::reader& dbr,
	const std::vector<std::wstring>& ambiguousEventNames,
	int measure,
	double threshold, double minThreshold
)
{
	// Retrieve similar strings into a string vector.
	std::vector<std::wstring> xstrs;
	for (; threshold > minThreshold/*kMinThreshold*/; threshold -= 0.05) {	// ���Ȃ��Ƃ����������悤��臒l��T��
		for (const std::wstring& query : ambiguousEventNames) {
			dbr.retrieve(query, measure, threshold, std::back_inserter(xstrs));
			if (xstrs.size()) {
				break;
			}
		}
		if (xstrs.size()) {
			break;
		}
	}
	if (xstrs.size()) {
		INFO_LOG << L"result: " << xstrs.front() << L" threshold: " << threshold;
		return xstrs.front();
	} else {
		return boost::none;
	}
}

boost::optional<std::wstring> retrieve(
	simstring::reader& dbr,
	const std::vector<std::wstring>& ambiguousEventNames,
	int measure,
	double threshold
)
{
	const double kMinThreshold = 0.4;
	return retrieve(dbr, ambiguousEventNames, measure, threshold, kMinThreshold);
}

void	EventNameNormalize(std::wstring& eventName)
{
	std::wregex rx(L"�i�i�s�x[^\\�j]+�j");
	eventName = std::regex_replace(eventName, rx, L"");
}

// ==============================================================

bool UmaEventLibrary::LoadUmaMusumeLibrary()
{
	m_charaEventList.clear();
	m_supportEventList.clear();
	try {
		auto funcAddjsonEventToUmaEvent = [](const json& jsonEventList, CharaEvent& charaEvent) {
			for (const json& jsonEvent : jsonEventList) {
				auto eventElm = *jsonEvent.items().begin();
				std::wstring eventName = UTF16fromUTF8(eventElm.key());
				EventNameNormalize(eventName);

				charaEvent.umaEventList.emplace_back();
				UmaEvent& umaEvent = charaEvent.umaEventList.back();
				umaEvent.eventName = eventName;

				int i = 0;
				for (const json& jsonOption : eventElm.value()) {
					std::wstring option = UTF16fromUTF8(jsonOption["Option"]);
					std::wstring effect = UTF16fromUTF8(jsonOption["Effect"]);
					boost::algorithm::replace_all(effect, L"\n", L"\r\n");

					if (kMaxOption <= i) {
						ATLASSERT(FALSE);
						throw std::runtime_error("�I�����̐��� kMaxOption �𒴂��܂�");
					}

					umaEvent.eventOptions[i].option = option;
					umaEvent.eventOptions[i].effect = effect;
					++i;
				}
			}
		};
		auto funcLoad = [=](const json& jsonLibrary, const std::string& keyName, CharaEventList& charaEventList) {
			for (const auto& propElm : jsonLibrary[keyName].items()) {
				std::wstring prop = UTF16fromUTF8(propElm.key());	// hosi or rare

				for (const auto& umaElm : propElm.value().items()) {
					std::wstring umaName = UTF16fromUTF8(umaElm.key());

					charaEventList.emplace_back(std::make_unique<CharaEvent>());
					CharaEvent& charaEvent = *charaEventList.back();
					charaEvent.name = umaName;
					charaEvent.property = prop;

					funcAddjsonEventToUmaEvent(umaElm.value()["Event"], charaEvent);
				}
			}
		};

		{	// UmaMusumeLibrary.json
			std::ifstream ifs((GetExeDirectory() / L"UmaLibrary" / "UmaMusumeLibrary.json").wstring());
			ATLASSERT(ifs);
			if (!ifs) {
				throw std::runtime_error("UmaMusumeLibrary.json �̓ǂݍ��݂Ɏ��s");
			}
			json jsonLibrary;
			ifs >> jsonLibrary;

			funcLoad(jsonLibrary, "Charactor", m_charaEventList);
			funcLoad(jsonLibrary, "Support", m_supportEventList);
		}
		{	// UmaMusumeLibraryMainStory.json
			std::ifstream ifs((GetExeDirectory() / L"UmaLibrary" / "UmaMusumeLibraryMainStory.json").wstring());
			ATLASSERT(ifs);
			if (!ifs) {
				throw std::runtime_error("UmaMusumeLibraryMainStory.json �̓ǂݍ��݂Ɏ��s");
			}
			json jsonLibrary;
			ifs >> jsonLibrary;

			funcLoad(jsonLibrary, "MainStory", m_supportEventList);
		}
		{	// UmaMusumeLibraryRevision.json
			std::ifstream ifs((GetExeDirectory() / L"UmaLibrary" / "UmaMusumeLibraryRevision.json").wstring());
			if (ifs) {
				json jsonRevisionLibrary;
				ifs >> jsonRevisionLibrary;
				for (const auto& keyVal : jsonRevisionLibrary.items()) {
					std::wstring sourceName = UTF16fromUTF8(keyVal.key());
					CharaEvent charaEvent;
					charaEvent.name = sourceName;
					funcAddjsonEventToUmaEvent(keyVal.value()["Event"], charaEvent);

					auto funcUpdateEventOptions = [](const CharaEvent& charaEvent, CharaEventList& charaEventList) -> bool {
						for (auto& anotherCharaEventList : charaEventList) {
							if (anotherCharaEventList->name == charaEvent.name) {	// �\�[�X��v
								bool update = false;
								for (auto& anotherUmaEventList : anotherCharaEventList->umaEventList) {
									for (const auto& umaEventList : charaEvent.umaEventList) {
										if (anotherUmaEventList.eventName == umaEventList.eventName) {	// �C�x���g����v
											anotherUmaEventList.eventOptions = umaEventList.eventOptions;	// �I�������㏑��
											update = true;
										}
									}
								}
								ATLASSERT(update);
								return true;
							}
						}
						return false;
					};
					// chara/supportEventList �֏㏑������
					if (!funcUpdateEventOptions(charaEvent, m_charaEventList)) {
						if (!funcUpdateEventOptions(charaEvent, m_supportEventList)) {
							// �C�x���g���X�g�ɃC�x���g�����Ȃ������̂ŁAm_supportEventList�֒ǉ����Ă���
							m_supportEventList.emplace_back(std::make_unique<CharaEvent>(charaEvent));
						}
					}
				}
			}
		}
		{
			std::ifstream ifs((GetExeDirectory() / L"UmaLibrary" / L"Common.json").wstring());
			ATLASSERT(ifs);
			if (!ifs) {
				throw std::runtime_error("Common.json �̓ǂݍ��݂Ɏ��s");
			}
			json jsonCommon;
			ifs >> jsonCommon;
			m_kMinThreshold = jsonCommon["Common"]["Simstring"].value<double>("MinThreshold", m_kMinThreshold);
		}

		_DBUmaNameInit();
		return true;
	}
	catch (std::exception& e) {
		ERROR_LOG << L"LoadUmaMusumeLibrary failed: " << (LPCWSTR)CA2W(e.what());
		ATLASSERT(FALSE);
	}
	return false;
}

void UmaEventLibrary::ChangeIkuseiUmaMusume(const std::wstring& umaName)
{
	std::unique_lock<std::mutex> lock(m_mtxName);	// �ꉞ�O�̂��߁c
	if (m_currentIkuseUmaMusume != umaName) {
		INFO_LOG << L"ChangeIkuseiUmaMusume: " << umaName;
		m_currentIkuseUmaMusume = umaName;
		m_simstringDBInit = false;
	}
}

void UmaEventLibrary::AnbigiousChangeIkuseImaMusume(std::vector<std::wstring> ambiguousUmaMusumeNames)
{
	// whilte space ����菜��
	for (auto& name : ambiguousUmaMusumeNames) {
		boost::algorithm::trim(name);
		boost::algorithm::replace_all(name, L" ", L"");
		boost::algorithm::replace_all(name, L"\n", L"");
	}

	// Output similar strings from Unicode queries.
	auto optResult = retrieve(*m_dbUmaNameReader, ambiguousUmaMusumeNames, simstring::cosine, 0.6, m_kMinThreshold);
	if (optResult) {
		ChangeIkuseiUmaMusume(optResult.get());
	}
}


boost::optional<UmaEventLibrary::UmaEvent> UmaEventLibrary::AmbiguousSearchEvent(
	const std::vector<std::wstring>& ambiguousEventNames,
	const std::vector<std::wstring>& ambiguousEventBottomOptions )
{
	_DBInit();

	m_lastEventSource.clear();

#ifdef _DEBUG

	auto optOptionResult = retrieve(*m_dbOptionReader, ambiguousEventBottomOptions, simstring::cosine, 1.0, m_kMinThreshold);
	auto optResult = retrieve(*m_dbReader, ambiguousEventNames, simstring::cosine, 1.0, m_kMinThreshold);

	UmaEvent event1 = optResult ? _SearchEventOptions(optResult.get()) : UmaEvent();
	UmaEvent event2 = optOptionResult ? _SearchEventOptionsFromBottomOption(optOptionResult.get()) : UmaEvent();
	if (event1.eventName != event2.eventName) {
		WARN_LOG << L"AmbiguousSearchEvent �C�x���g���s��v\n"
			<< L"�E�C�x���g������ 1: [" << event1.eventName << L"] (" << ambiguousEventNames.front() << L")\n"
			<< L"�E�����I�������� 2: [" << event2.eventName << L"] (" << ambiguousEventBottomOptions.front() << L")";
	}

	if (optOptionResult) {	// �I��������̌�����D�悷��
		INFO_LOG << L"AmbiguousSearchEvent result: " << event2.eventName;
		return _SearchEventOptionsFromBottomOption(optOptionResult.get());
	}
	if (optResult) {
		INFO_LOG << L"AmbiguousSearchEvent result: " << event1.eventName;
		return _SearchEventOptions(optResult.get());

	} else {
		INFO_LOG << L"AmbiguousSearchEvent: not found";
		return boost::none;
	}
#else

	auto optOptionResult = retrieve(*m_dbOptionReader, ambiguousEventBottomOptions, simstring::cosine, 1.0, m_kMinThreshold);
	if (optOptionResult) {	// �I��������̌�����D�悷��
		return _SearchEventOptionsFromBottomOption(optOptionResult.get());
	}
	auto optResult = retrieve(*m_dbReader, ambiguousEventNames, simstring::cosine, 1.0, m_kMinThreshold);
	if (optResult) {
		return _SearchEventOptions(optResult.get());

	} else {
		return boost::none;
	}
#endif
}

void UmaEventLibrary::_DBUmaNameInit()
{
	m_dbUmaNameReader = std::make_unique<simstring::reader>();
	auto dbFolder = GetExeDirectory() / L"simstringDB" / L"UmaName";
	auto dbPath = dbFolder / L"umaName_unicode.db";

	// DB�t�H���_���������ď�����
	if (boost::filesystem::is_directory(dbFolder)) {
		boost::system::error_code ec = {};
		boost::filesystem::remove_all(dbFolder, ec);
		if (ec) {
			ERROR_LOG << L"boost::filesystem::remove_all(dbFolder failed: " << (LPCWSTR)CA2W(ec.message().c_str());
		}
	}
	boost::filesystem::create_directories(dbFolder);

	// Open a SimString database for writing (with std::wstring).
	simstring::ngram_generator gen;
	simstring::writer_base<std::wstring> dbw(gen, dbPath.string());

	// �琬�E�}���̖��O��ǉ�
	for (const auto& charaEvent : m_charaEventList) {
		const std::wstring& name = charaEvent->name;
		dbw.insert(name);
	}
	dbw.close();

	// Open the database for reading.
	m_dbUmaNameReader->open(dbPath.string());
}

void UmaEventLibrary::_DBInit()
{
	if (!m_simstringDBInit) {
		auto dbFolder = GetExeDirectory() / L"simstringDB" / L"Event";

		m_dbReader = std::make_unique<simstring::reader>();
		auto dbPath = dbFolder / L"event_unicode.db";

		m_dbOptionReader = std::make_unique<simstring::reader>();
		auto dbOptionPath = dbFolder / L"eventOption_unicode.db";

		// DB�t�H���_���������ď�����
		if (boost::filesystem::is_directory(dbFolder)) {
			boost::system::error_code ec = {};
			boost::filesystem::remove_all(dbFolder, ec);
			if (ec) {
				ERROR_LOG << L"boost::filesystem::remove_all(dbFolder failed: " << (LPCWSTR)CA2W(ec.message().c_str());
			}
		}
		boost::filesystem::create_directories(dbFolder);

		// Open a SimString database for writing (with std::wstring).
		simstring::ngram_generator gen(2, false);	// bi-gram
		// �C�x���g��DB
		simstring::writer_base<std::wstring> dbw(gen, dbPath.string());
		// �C�x���g�I����DB
		simstring::writer_base<std::wstring> dbwOption(gen, dbOptionPath.string());


		// �琬�E�}���̃C�x���g��ǉ�
		for (const auto& charaEvent : m_charaEventList) {
			if (charaEvent->name.find(m_currentIkuseUmaMusume) == std::wstring::npos) {
				continue;
			}

			for (const auto& umaEvent : charaEvent->umaEventList) {
				dbw.insert(umaEvent.eventName);

				for (auto it = umaEvent.eventOptions.crbegin(); it != umaEvent.eventOptions.crend(); ++it) {
					if (it->option.empty()) {
						continue;
					}
					dbwOption.insert(it->option);	// �Ō�̑I������ǉ�
					break;
				}
			}
			break;
		}

		// �T�|�[�g�J�[�h�̃C�x���g��ǉ�
		for (const auto& charaEvent : m_supportEventList) {
			for (const auto& umaEvent : charaEvent->umaEventList) {
				dbw.insert(umaEvent.eventName);

				for (auto it = umaEvent.eventOptions.crbegin(); it != umaEvent.eventOptions.crend(); ++it) {
					if (it->option.empty()) {
						continue;
					}
					dbwOption.insert(it->option);	// �Ō�̑I������ǉ�
					break;
				}
			}
		}
		
		dbw.close();
		dbwOption.close();

		// Open the database for reading.
		m_dbReader->open(dbPath.string());
		m_dbOptionReader->open(dbOptionPath.string());

		m_simstringDBInit = true;
	}
}

UmaEventLibrary::UmaEvent UmaEventLibrary::_SearchEventOptions(const std::wstring& eventName)
{
	// �琬�E�}���̃C�x���g��T��
	for (const auto& charaEvent : m_charaEventList) {
		if (charaEvent->name.find(m_currentIkuseUmaMusume) == std::wstring::npos) {
			continue;
		}

		for (const auto& umaEvent : charaEvent->umaEventList) {
			if (umaEvent.eventName == eventName) {
				m_lastEventSource = charaEvent->name;
				return umaEvent;
			}
		}
		break;
	}

	// �T�|�[�g�J�[�h�̃C�x���g��T��
	for (const auto& charaEvent : m_supportEventList) {
		for (const auto& umaEvent : charaEvent->umaEventList) {
			if (umaEvent.eventName == eventName) {
				m_lastEventSource = charaEvent->name;
				return umaEvent;
			}
		}
	}
	ATLASSERT(FALSE);
	return UmaEvent();
}

UmaEventLibrary::UmaEvent UmaEventLibrary::_SearchEventOptionsFromBottomOption(const std::wstring& bottomOption)
{
	// �琬�E�}���̃C�x���g��T��
	for (const auto& charaEvent : m_charaEventList) {
		if (charaEvent->name.find(m_currentIkuseUmaMusume) == std::wstring::npos) {
			continue;
		}

		for (const auto& umaEvent : charaEvent->umaEventList) {
			for (auto it = umaEvent.eventOptions.crbegin(); it != umaEvent.eventOptions.crend(); ++it) {
				if (it->option.empty()) {
					continue;
				}
				if (it->option == bottomOption) {	// �Ō�̑I�������r
					m_lastEventSource = charaEvent->name;
					return umaEvent;
				}
				break;
			}
		}
		break;
	}

	// �T�|�[�g�J�[�h�̃C�x���g��T��
	for (const auto& charaEvent : m_supportEventList) {
		for (const auto& umaEvent : charaEvent->umaEventList) {
			for (auto it = umaEvent.eventOptions.crbegin(); it != umaEvent.eventOptions.crend(); ++it) {
				if (it->option.empty()) {
					continue;
				}
				if (it->option == bottomOption) {	// �Ō�̑I�������r
					m_lastEventSource = charaEvent->name;
					return umaEvent;
				}
				break;
			}
		}
	}
	ATLASSERT(FALSE);
	return UmaEvent();
}
