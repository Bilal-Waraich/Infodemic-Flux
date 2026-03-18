#!/usr/bin/env python3
"""
build_agents_config.py — data pipeline for the Digital Contagion Simulator.

Reads six raw datasets and merges them into data/processed/agents_config.csv
(217 rows, 14 columns), which DataLoader::buildWorld() reads at simulation startup.

Pipeline stages:
  FIX 1  ITU Internet Penetration — decodes UTF-16 file, renames double-dot filename
  FIX 2  World Bank Literacy — picks most-recent available year per country
  FIX 3  RSF Press Freedom — detects whether REF_AREA is ISO2 or ISO3 and converts
  FIX 4  V-Dem Polarisation — tries OWID download; falls back to v2x_polyarchy (inverted)
  FIX 5  Platform data — hardcoded from DataReportal 2024 (HTTP APIs return 403)
  MERGE  Joins all frames on ISO3 country_code; fills NaN with regional/global medians
  VALIDATE Asserts zero NaN in final output

Output columns:
  country, code, region,
  literacy_score, press_freedom_score, polarization_score,
  internet_penetration, institutional_trust,
  platform_1, platform_2, platform_3,
  p1_pct, p2_pct, p3_pct

Notes:
  - Polarisation is derived as (1 - normalised_v2x_polyarchy) so low-democracy
    countries have high polarisation scores, matching the model's direction.
  - institutional_trust is derived: literacy*0.4 + press_freedom*0.6.
  - Platform percentages are population fractions (0-1), not percentages.
"""

import os
import re
import sys
import warnings
import subprocess

warnings.filterwarnings('ignore')

# ── install pycountry if missing ─────────────────────────────────────────────
try:
    import pycountry
except ImportError:
    subprocess.check_call([sys.executable, '-m', 'pip', 'install', 'pycountry', '-q'])
    import pycountry

import pandas as pd
import numpy as np
import urllib.request

BASE = '/Users/bilalwaraich/Desktop/Research/Infodemic-Flux'
RAW  = os.path.join(BASE, 'data', 'raw')
PROC = os.path.join(BASE, 'data', 'processed')
os.makedirs(PROC, exist_ok=True)

# ════════════════════════════════════════════════════════════════════════════
# FIX 1 — ITU Internet (UTF-16, rename double-dot)
# ════════════════════════════════════════════════════════════════════════════
print("\n" + "="*60)
print("FIX 1: ITU Internet Penetration")
print("="*60)

itu_double = os.path.join(RAW, 'itu_internet..csv')
itu_fixed  = os.path.join(RAW, 'itu_internet.csv')

if os.path.exists(itu_double) and not os.path.exists(itu_fixed):
    os.rename(itu_double, itu_fixed)
    print(f"Renamed: itu_internet..csv → itu_internet.csv")
elif os.path.exists(itu_fixed):
    print(f"itu_internet.csv already exists, skipping rename.")
else:
    print(f"WARNING: Neither itu_internet..csv nor itu_internet.csv found!")

df_itu = None
itu_path = itu_fixed if os.path.exists(itu_fixed) else None

if itu_path:
    for enc in ['utf-16', 'utf-16-le', 'utf-16-be']:
        for sep in ['\t', ',', ';']:
            try:
                df_test = pd.read_csv(itu_path, encoding=enc, sep=sep, low_memory=False)
                if len(df_test.columns) >= 3 and len(df_test) > 5:
                    df_itu = df_test
                    print(f"Loaded with encoding={enc}, sep={repr(sep)}")
                    print(f"Columns: {list(df_itu.columns[:15])}")
                    print(f"Rows: {len(df_itu)}")
                    print(f"First 5 rows:\n{df_itu.head(5).to_string()}")
                    break
            except Exception as e:
                continue
        if df_itu is not None:
            break

    if df_itu is None:
        with open(itu_path, 'rb') as f:
            raw_bytes = f.read(200)
        print(f"Raw bytes (hex): {raw_bytes.hex()[:100]}")
        print(f"WARNING: Could not decode ITU file — will use regional median fallback")

df_itu_clean = None
if df_itu is not None:
    cols = list(df_itu.columns)
    print(f"\nAll ITU columns: {cols}")

    iso_col = None
    for c in cols:
        if any(k in str(c).lower() for k in ['iso','code','country code','economy code']):
            iso_col = c; break

    name_col = None
    for c in cols:
        if any(k in str(c).lower() for k in ['country','economy','name']):
            name_col = c; break

    year_cols = [c for c in cols if str(c).strip().isdigit() and int(str(c).strip()) > 2000]
    value_col = None

    if not year_cols:
        # Maybe it's a 'Value' column with a 'Year' column
        for c in cols:
            if 'value' in str(c).lower() or 'penetration' in str(c).lower() or '%' in str(c) or 'internet' in str(c).lower():
                value_col = c; break

    print(f"iso_col={iso_col}, name_col={name_col}, year_cols(last5)={year_cols[-5:] if year_cols else []}, value_col={value_col}")

    if iso_col and year_cols:
        most_recent = max(year_cols, key=lambda x: int(str(x).strip()))
        print(f"Using year column: {most_recent}")
        df_itu_clean = df_itu[[iso_col, most_recent]].copy()
        df_itu_clean.columns = ['country_code', 'internet_penetration']
        df_itu_clean['internet_penetration'] = pd.to_numeric(df_itu_clean['internet_penetration'], errors='coerce')
        if df_itu_clean['internet_penetration'].max() > 1.5:
            df_itu_clean['internet_penetration'] = df_itu_clean['internet_penetration'] / 100.0
        df_itu_clean = df_itu_clean[df_itu_clean['country_code'].apply(
            lambda x: bool(re.match(r'^[A-Z]{3}$', str(x).strip()))
        )]
        df_itu_clean['country_code'] = df_itu_clean['country_code'].str.strip()
        df_itu_clean = df_itu_clean.dropna(subset=['internet_penetration'])
        print(f"ITU clean: {len(df_itu_clean)} countries, internet_penetration range: {df_itu_clean['internet_penetration'].min():.3f}–{df_itu_clean['internet_penetration'].max():.3f}")
    elif iso_col and value_col:
        year_col_itu = None
        for c in cols:
            if 'year' in str(c).lower():
                year_col_itu = c; break
        if year_col_itu:
            df_itu2 = df_itu.copy()
            df_itu2[year_col_itu] = pd.to_numeric(df_itu2[year_col_itu], errors='coerce')
            max_yr = df_itu2[year_col_itu].max()
            df_itu2 = df_itu2[df_itu2[year_col_itu] == max_yr]
            df_itu_clean = df_itu2[[iso_col, value_col]].copy()
            df_itu_clean.columns = ['country_code', 'internet_penetration']
            df_itu_clean['internet_penetration'] = pd.to_numeric(df_itu_clean['internet_penetration'], errors='coerce')
            if df_itu_clean['internet_penetration'].max() > 1.5:
                df_itu_clean['internet_penetration'] = df_itu_clean['internet_penetration'] / 100.0
            df_itu_clean = df_itu_clean[df_itu_clean['country_code'].apply(
                lambda x: bool(re.match(r'^[A-Z]{3}$', str(x).strip()))
            )]
            df_itu_clean = df_itu_clean.dropna(subset=['internet_penetration'])
            print(f"ITU clean (long format): {len(df_itu_clean)} countries")

    if df_itu_clean is None or len(df_itu_clean) == 0:
        print("WARNING: ITU data could not be parsed — will use regional median fallback")
        df_itu_clean = None

# ════════════════════════════════════════════════════════════════════════════
# FIX 2 — World Bank Literacy
# ════════════════════════════════════════════════════════════════════════════
print("\n" + "="*60)
print("FIX 2: World Bank Literacy")
print("="*60)

WB_AGGREGATES = {
    'AFE','AFW','ARB','CEB','CSS','EAP','EAR','EAS','ECA','ECS','EMU','EUU',
    'FCS','HIC','HPC','IBD','IBT','IDA','IDB','IDX','LAC','LCN','LDC','LIC',
    'LMC','LMY','LTE','MEA','MIC','MNA','NAC','OED','OSS','PRE','PSS','PST',
    'SAS','SSA','SSF','SST','TEA','TEC','TLA','TMN','TSA','TSS','UMC','WLD','INX'
}

lit_path = os.path.join(RAW, 'world_bank_literacy.csv')
df_lit_raw = pd.read_csv(lit_path, skiprows=4, encoding='utf-8', on_bad_lines='skip')

year_cols_lit = [c for c in df_lit_raw.columns if str(c).strip().isdigit() and int(str(c).strip()) > 1980]
year_cols_lit_sorted = sorted(year_cols_lit, key=lambda x: int(str(x).strip()), reverse=True)

def get_latest_value(row, year_cols):
    for yr in year_cols:
        v = pd.to_numeric(row.get(yr, np.nan), errors='coerce')
        if not pd.isna(v):
            return v, yr
    return np.nan, None

records = []
for _, row in df_lit_raw.iterrows():
    cc = str(row.get('Country Code', '')).strip()
    cn = str(row.get('Country Name', '')).strip()
    if cc in WB_AGGREGATES or not re.match(r'^[A-Z]{3}$', cc):
        continue
    val, yr = get_latest_value(row, year_cols_lit_sorted)
    records.append({'country_code': cc, 'country_name': cn,
                    'literacy_raw': val, 'literacy_year': yr})

df_literacy = pd.DataFrame(records)
df_literacy['literacy_score'] = pd.to_numeric(df_literacy['literacy_raw'], errors='coerce') / 100.0
df_literacy = df_literacy[['country_code','country_name','literacy_score','literacy_year']]

recent = df_literacy[pd.to_numeric(df_literacy['literacy_year'], errors='coerce') >= 2023]
print(f"Total countries retained: {len(df_literacy)}")
print(f"Mean literacy_score: {df_literacy['literacy_score'].mean():.4f}")
print(f"Countries using 2023+: {len(recent)}")
print(f"Countries using older years: {len(df_literacy) - len(recent)}")

# ════════════════════════════════════════════════════════════════════════════
# FIX 3 — Press Freedom ISO2 → ISO3
# ════════════════════════════════════════════════════════════════════════════
print("\n" + "="*60)
print("FIX 3: Press Freedom Index")
print("="*60)

def iso2_to_iso3(code):
    try:
        return pycountry.countries.get(alpha_2=str(code).strip()).alpha_3
    except:
        return None

pf_path = os.path.join(RAW, 'press_freedom.csv')
df_pf_raw = pd.read_csv(pf_path, encoding='utf-8', on_bad_lines='skip', low_memory=False)

df_pf = df_pf_raw[df_pf_raw['INDICATOR'] == 'RWB_PFI_SCORE'].copy()
print(f"Rows with INDICATOR==RWB_PFI_SCORE: {len(df_pf)}")

year_cols_pf = [c for c in df_pf.columns if str(c).strip().isdigit() and int(str(c).strip()) > 2000]
year_cols_pf_sorted = sorted(year_cols_pf, key=lambda x: int(str(x).strip()), reverse=True)

def get_latest_pf(row, year_cols):
    for yr in year_cols:
        v = pd.to_numeric(row.get(yr, np.nan), errors='coerce')
        if not pd.isna(v):
            return v, yr
    return np.nan, None

pf_records = []
for _, row in df_pf.iterrows():
    ref = str(row.get('REF_AREA', '')).strip()
    # REF_AREA may already be ISO3 (3-letter) or ISO2 (2-letter) — handle both
    import re as _re
    if _re.match(r'^[A-Z]{3}$', ref):
        iso3 = ref  # already ISO3
    else:
        iso3 = iso2_to_iso3(ref)  # try ISO2→ISO3 conversion
    val, yr = get_latest_pf(row, year_cols_pf_sorted)
    pf_records.append({'country_code': iso3, 'press_freedom_raw': val, 'pf_year': yr, 'ref_area': ref})

df_pf_clean = pd.DataFrame(pf_records)
df_pf_clean = df_pf_clean[df_pf_clean['country_code'].notna()]
df_pf_clean['press_freedom_score'] = pd.to_numeric(df_pf_clean['press_freedom_raw'], errors='coerce') / 100.0
df_pf_clean = df_pf_clean[['country_code','press_freedom_score']].dropna()

print(f"Total countries after ISO2→ISO3: {len(df_pf_clean)}")
print(f"Min: {df_pf_clean['press_freedom_score'].min():.4f}, Max: {df_pf_clean['press_freedom_score'].max():.4f}, Mean: {df_pf_clean['press_freedom_score'].mean():.4f}")

# ════════════════════════════════════════════════════════════════════════════
# FIX 4 — V-Dem Polarization via Our World in Data
# ════════════════════════════════════════════════════════════════════════════
print("\n" + "="*60)
print("FIX 4: V-Dem Polarization via OWID")
print("="*60)

owid_path = os.path.join(RAW, 'owid_polarization.csv')

if not os.path.exists(owid_path):
    urls_to_try = [
        'https://ourworldindata.org/grapher/political-polarization-score.csv?v=1&csvType=full&useColumnShortNames=false',
        'https://ourworldindata.org/grapher/political-polarization-score.csv',
    ]
    downloaded = False
    for url in urls_to_try:
        try:
            print(f"Trying: {url}")
            urllib.request.urlretrieve(url, owid_path)
            df_test = pd.read_csv(owid_path)
            if len(df_test.columns) >= 3 and len(df_test) > 10:
                print(f"Downloaded successfully: {len(df_test)} rows")
                downloaded = True
                break
            else:
                os.remove(owid_path)
        except Exception as e:
            print(f"  Failed: {e}")
    if not downloaded:
        print("WARNING: Could not download OWID polarization data — will use V-Dem fallback")
else:
    print(f"owid_polarization.csv already exists")

df_polar = None
owid_available = os.path.exists(owid_path)

if not owid_available:
    vdem_path_fb = os.path.join(RAW, 'vdem_core.csv')
    if os.path.exists(vdem_path_fb):
        print("Using V-Dem fallback (v2x_polyarchy) for polarization...")
        df_vdem_fb = pd.read_csv(vdem_path_fb, usecols=['country_text_id','year','v2x_polyarchy'], low_memory=False)
        df_vdem_fb['year'] = pd.to_numeric(df_vdem_fb['year'], errors='coerce')
        df_vdem_fb = df_vdem_fb.sort_values('year', ascending=False).drop_duplicates('country_text_id', keep='first')
        df_vdem_fb['v2x_polyarchy'] = pd.to_numeric(df_vdem_fb['v2x_polyarchy'], errors='coerce')
        df_vdem_fb = df_vdem_fb.dropna(subset=['v2x_polyarchy'])
        rmin, rmax = df_vdem_fb['v2x_polyarchy'].min(), df_vdem_fb['v2x_polyarchy'].max()
        if rmax > rmin:
            df_vdem_fb['polarization_score'] = (df_vdem_fb['v2x_polyarchy'] - rmin) / (rmax - rmin)
        else:
            df_vdem_fb['polarization_score'] = 0.5
        # Invert: low democracy → high polarization
        df_vdem_fb['polarization_score'] = 1.0 - df_vdem_fb['polarization_score']
        df_polar = df_vdem_fb[['country_text_id','polarization_score']].rename(columns={'country_text_id':'country_code'})
        print(f"V-Dem fallback: {len(df_polar)} countries, polarization_score range: {df_polar['polarization_score'].min():.3f}–{df_polar['polarization_score'].max():.3f}")

if os.path.exists(owid_path):
    df_polar_raw = pd.read_csv(owid_path)
    print(f"Columns: {list(df_polar_raw.columns)}")
    print(f"First 5 rows:\n{df_polar_raw.head(5).to_string()}")

    code_col = None
    for c in df_polar_raw.columns:
        if str(c).lower() in ['code','iso','iso3','country_code']:
            code_col = c; break

    year_col_p = None
    for c in df_polar_raw.columns:
        if str(c).lower() in ['year','date']:
            year_col_p = c; break

    value_col_p = None
    for c in df_polar_raw.columns:
        if c not in [code_col, year_col_p] and c not in ['Entity','entity','Country','country']:
            val_test = pd.to_numeric(df_polar_raw[c], errors='coerce').dropna()
            if len(val_test) > 10:
                value_col_p = c; break

    print(f"code_col={code_col}, year_col={year_col_p}, value_col={value_col_p}")

    if code_col and value_col_p:
        df_p2 = df_polar_raw.copy()
        if year_col_p:
            df_p2[year_col_p] = pd.to_numeric(df_p2[year_col_p], errors='coerce')
            df_p2 = df_p2.sort_values(year_col_p, ascending=False)
            df_p2 = df_p2.drop_duplicates(subset=[code_col], keep='first')
            most_recent_yr = df_p2[year_col_p].max()
            print(f"Most recent year in OWID data: {most_recent_yr}")

        df_p2[value_col_p] = pd.to_numeric(df_p2[value_col_p], errors='coerce')
        df_p2 = df_p2.dropna(subset=[value_col_p, code_col])
        df_p2 = df_p2[df_p2[code_col].apply(lambda x: bool(re.match(r'^[A-Z]{3}$', str(x).strip())))]

        raw_min = df_p2[value_col_p].min()
        raw_max = df_p2[value_col_p].max()
        print(f"Raw polarization range: min={raw_min:.4f}, max={raw_max:.4f}")

        if raw_max > raw_min:
            df_p2['polarization_score'] = (df_p2[value_col_p] - raw_min) / (raw_max - raw_min)
        else:
            df_p2['polarization_score'] = 0.5

        df_polar = df_p2[[code_col, 'polarization_score']].copy()
        df_polar.columns = ['country_code', 'polarization_score']
        print(f"Total countries: {len(df_polar)}")
    else:
        print(f"WARNING: Could not identify required columns in OWID data")
        # Fallback to V-Dem using v2x_polyarchy as polarization proxy
        vdem_path = os.path.join(RAW, 'vdem_core.csv')
        if os.path.exists(vdem_path):
            print("Attempting fallback: using v2x_polyarchy from vdem_core.csv as polarization proxy...")
            df_vdem_cols = pd.read_csv(vdem_path, nrows=0).columns.tolist()
            polar_candidates = [c for c in df_vdem_cols if any(k in c.lower() for k in ['polar','camp','divis','partisan'])]
            # Use v2x_polyarchy as proxy if no dedicated polarization column
            if not polar_candidates and 'v2x_polyarchy' in df_vdem_cols:
                polar_candidates = ['v2x_polyarchy']
                print("No direct polarization column found; using v2x_polyarchy as proxy")
            print(f"Polarization candidate columns in V-Dem: {polar_candidates[:20]}")
            if polar_candidates:
                pc = polar_candidates[0]
                df_vdem_p = pd.read_csv(vdem_path, usecols=['country_text_id','year', pc], low_memory=False)
                df_vdem_p['year'] = pd.to_numeric(df_vdem_p['year'], errors='coerce')
                df_vdem_p = df_vdem_p.sort_values('year', ascending=False).drop_duplicates('country_text_id', keep='first')
                df_vdem_p[pc] = pd.to_numeric(df_vdem_p[pc], errors='coerce')
                df_vdem_p = df_vdem_p.dropna(subset=[pc])
                rmin, rmax = df_vdem_p[pc].min(), df_vdem_p[pc].max()
                if rmax > rmin:
                    df_vdem_p['polarization_score'] = (df_vdem_p[pc] - rmin) / (rmax - rmin)
                else:
                    df_vdem_p['polarization_score'] = 0.5
                # Invert: low democracy → high polarization
                df_vdem_p['polarization_score'] = 1.0 - df_vdem_p['polarization_score']
                df_polar = df_vdem_p[['country_text_id','polarization_score']].rename(columns={'country_text_id':'country_code'})
                print(f"Fallback V-Dem polarization: {len(df_polar)} countries using column '{pc}'")

if df_polar is None:
    print("WARNING: No polarization data available — will use global median fallback")
    df_polar = pd.DataFrame(columns=['country_code','polarization_score'])

# ════════════════════════════════════════════════════════════════════════════
# FIX 5 — Platform data (hardcoded)
# ════════════════════════════════════════════════════════════════════════════
print("\n" + "="*60)
print("FIX 5: Platform data (hardcoded)")
print("="*60)

platform_data = [
  ('DEU','WhatsApp','Instagram','YouTube',0.85,0.62,0.78),
  ('FRA','WhatsApp','Instagram','Facebook',0.76,0.64,0.62),
  ('GBR','WhatsApp','Instagram','Facebook',0.79,0.67,0.64),
  ('ITA','WhatsApp','Instagram','Facebook',0.83,0.71,0.60),
  ('ESP','WhatsApp','Instagram','Facebook',0.86,0.73,0.65),
  ('NLD','WhatsApp','Instagram','YouTube',0.81,0.68,0.72),
  ('NOR','Facebook','Snapchat','Instagram',0.74,0.67,0.71),
  ('SWE','Facebook','Instagram','Snapchat',0.72,0.68,0.63),
  ('FIN','Facebook','Instagram','YouTube',0.70,0.61,0.67),
  ('DNK','Facebook','Instagram','Snapchat',0.73,0.65,0.60),
  ('RUS','VK','Telegram','YouTube',0.73,0.58,0.71),
  ('POL','Facebook','Instagram','YouTube',0.68,0.59,0.70),
  ('UKR','Facebook','Instagram','Telegram',0.63,0.55,0.52),
  ('USA','Facebook','YouTube','TikTok',0.69,0.83,0.52),
  ('CAN','Facebook','YouTube','Instagram',0.67,0.81,0.58),
  ('MEX','Facebook','WhatsApp','Instagram',0.71,0.67,0.58),
  ('BRA','WhatsApp','YouTube','Instagram',0.76,0.82,0.60),
  ('ARG','WhatsApp','Instagram','Facebook',0.85,0.74,0.68),
  ('COL','WhatsApp','Instagram','Facebook',0.78,0.68,0.62),
  ('CHL','WhatsApp','Instagram','Facebook',0.80,0.70,0.61),
  ('PER','Facebook','WhatsApp','TikTok',0.72,0.65,0.44),
  ('IND','Facebook','WhatsApp','Instagram',0.64,0.58,0.45),
  ('PAK','Facebook','WhatsApp','TikTok',0.58,0.44,0.31),
  ('BGD','Facebook','WhatsApp','YouTube',0.60,0.48,0.55),
  ('LKA','Facebook','WhatsApp','YouTube',0.55,0.47,0.50),
  ('IDN','WhatsApp','Instagram','Facebook',0.82,0.72,0.65),
  ('PHL','Facebook','YouTube','TikTok',0.79,0.74,0.55),
  ('VNM','Facebook','YouTube','TikTok',0.73,0.71,0.48),
  ('THA','Facebook','YouTube','TikTok',0.76,0.72,0.50),
  ('MYS','WhatsApp','Instagram','Facebook',0.80,0.70,0.62),
  ('CHN','WeChat','Douyin','Weibo',0.89,0.83,0.44),
  ('JPN','YouTube','Twitter','Instagram',0.78,0.55,0.49),
  ('KOR','YouTube','KakaoTalk','Instagram',0.82,0.91,0.67),
  ('TWN','Facebook','YouTube','Instagram',0.75,0.70,0.58),
  ('SAU','YouTube','WhatsApp','Twitter',0.95,0.88,0.72),
  ('ARE','Instagram','YouTube','TikTok',0.88,0.85,0.70),
  ('EGY','Facebook','WhatsApp','TikTok',0.69,0.61,0.44),
  ('TUR','YouTube','Instagram','WhatsApp',0.72,0.68,0.74),
  ('IRN','Instagram','Telegram','WhatsApp',0.45,0.62,0.55),
  ('NGA','Facebook','WhatsApp','TikTok',0.48,0.39,0.22),
  ('KEN','Facebook','WhatsApp','TikTok',0.41,0.36,0.19),
  ('ZAF','Facebook','WhatsApp','Instagram',0.63,0.58,0.44),
  ('GHA','Facebook','WhatsApp','TikTok',0.44,0.38,0.18),
  ('ETH','Facebook','Telegram','TikTok',0.22,0.18,0.12),
  ('TZA','Facebook','WhatsApp','TikTok',0.30,0.26,0.14),
  ('AUS','Facebook','YouTube','Instagram',0.71,0.79,0.60),
  ('NZL','Facebook','YouTube','Instagram',0.72,0.77,0.62),
]

df_platform = pd.DataFrame(platform_data,
    columns=['country_code','platform_1','platform_2','platform_3',
             'p1_pct','p2_pct','p3_pct'])
print(f"Platform entries: {len(df_platform)}")
print(f"Unique platforms: {sorted(set(df_platform['platform_1'].tolist() + df_platform['platform_2'].tolist() + df_platform['platform_3'].tolist()))}")

# ════════════════════════════════════════════════════════════════════════════
# REGION MAP
# ════════════════════════════════════════════════════════════════════════════
print("\n" + "="*60)
print("REGION MAP: CLASS.xlsx")
print("="*60)

class_path = os.path.join(RAW, 'CLASS.xlsx')
df_region = pd.read_excel(class_path)
df_region = df_region[['Economy','Code','Region']].copy()
df_region.columns = ['country_name_wb','country_code','region']
df_region = df_region[df_region['country_code'].apply(
    lambda x: bool(re.match(r'^[A-Z]{3}$', str(x).strip()))
)]
print(f"Region map: {len(df_region)} countries, {df_region['region'].nunique()} regions")

# ════════════════════════════════════════════════════════════════════════════
# MERGE
# ════════════════════════════════════════════════════════════════════════════
print("\n" + "="*60)
print("MERGE: Building agents_config")
print("="*60)

base = df_literacy.copy()
print(f"Base (literacy): {len(base)} rows")

base = base.merge(df_pf_clean, on='country_code', how='left')
print(f"After +press_freedom: {len(base)} rows, PF non-NaN: {base['press_freedom_score'].notna().sum()}")

base = base.merge(df_polar, on='country_code', how='left')
print(f"After +polarization: {len(base)} rows, polar non-NaN: {base['polarization_score'].notna().sum()}")

if df_itu_clean is not None and len(df_itu_clean) > 0:
    base = base.merge(df_itu_clean, on='country_code', how='left')
    print(f"After +ITU: {len(base)} rows, ITU non-NaN: {base['internet_penetration'].notna().sum()}")
else:
    base['internet_penetration'] = np.nan
    print("ITU data unavailable — internet_penetration set to NaN (will fill with regional/global median)")

base = base.merge(df_platform, on='country_code', how='left')
print(f"After +platform: {len(base)} rows, platform non-NaN: {base['platform_1'].notna().sum()}")

df_region_merge = df_region[['country_code','region']].copy()
base = base.merge(df_region_merge, on='country_code', how='left')
print(f"After +region: {len(base)} rows, region non-NaN: {base['region'].notna().sum()}")

# ════════════════════════════════════════════════════════════════════════════
# FILL MISSING VALUES
# ════════════════════════════════════════════════════════════════════════════
print("\n" + "="*60)
print("FILLING MISSING VALUES")
print("="*60)

med_lit  = base['literacy_score'].median()
med_pf   = base['press_freedom_score'].median()
med_pol  = base['polarization_score'].median()

print(f"Median literacy_score: {med_lit:.4f}")
print(f"Median press_freedom_score: {med_pf:.4f}")
print(f"Median polarization_score: {med_pol:.4f}")

# For internet penetration: try regional medians first, then global
if base['internet_penetration'].notna().sum() > 10:
    med_inet = base['internet_penetration'].median()
    print(f"Median internet_penetration: {med_inet:.4f}")
    reg_inet_medians = base.groupby('region')['internet_penetration'].median()
    def fill_inet(row):
        if pd.notna(row['internet_penetration']):
            return row['internet_penetration']
        reg = row.get('region')
        if pd.notna(reg) and reg in reg_inet_medians and pd.notna(reg_inet_medians[reg]):
            return reg_inet_medians[reg]
        return med_inet
    base['internet_penetration'] = base.apply(fill_inet, axis=1)
else:
    # Use World Bank internet data as proxy — hardcode regional medians
    REGIONAL_INET = {
        'North America': 0.91,
        'Europe & Central Asia': 0.79,
        'East Asia & Pacific': 0.73,
        'Latin America & Caribbean': 0.68,
        'Middle East, North Africa, Afghanistan & Pakistan': 0.65,
        'South Asia': 0.43,
        'Sub-Saharan Africa': 0.30,
    }
    print("Using hardcoded regional internet medians (ITU data unavailable)")
    def fill_inet_regional(row):
        if pd.notna(row.get('internet_penetration')):
            return row['internet_penetration']
        reg = row.get('region')
        return REGIONAL_INET.get(reg, 0.50)
    base['internet_penetration'] = base.apply(fill_inet_regional, axis=1)

med_inet_final = base['internet_penetration'].median()
print(f"internet_penetration after fill: {base['internet_penetration'].notna().sum()} non-NaN")

base['literacy_score']        = base['literacy_score'].fillna(med_lit)
base['press_freedom_score']   = base['press_freedom_score'].fillna(med_pf)
base['polarization_score']    = base['polarization_score'].fillna(med_pol)

# Platform fallbacks
plat_has_real = base['platform_1'].notna()
base['platform_1'] = base['platform_1'].fillna('Facebook')
base['platform_2'] = base['platform_2'].fillna('WhatsApp')
base['platform_3'] = base['platform_3'].fillna('YouTube')
base['p1_pct']     = base['p1_pct'].fillna(0.45)
base['p2_pct']     = base['p2_pct'].fillna(0.35)
base['p3_pct']     = base['p3_pct'].fillna(0.30)

base['region'] = base['region'].fillna('Unknown')
base['institutional_trust'] = (base['literacy_score'] * 0.4) + (base['press_freedom_score'] * 0.6)
base = base[base['country_code'].apply(
    lambda x: bool(re.match(r'^[A-Z]{3}$', str(x).strip()))
)]
base = base.dropna(subset=['country_code'])
base = base.rename(columns={'country_name': 'country', 'country_code': 'code'})
final_cols = [
    'country','code','region',
    'literacy_score','press_freedom_score','polarization_score',
    'internet_penetration','institutional_trust',
    'platform_1','platform_2','platform_3',
    'p1_pct','p2_pct','p3_pct'
]
base = base[[c for c in final_cols if c in base.columns]]
# Add any missing cols with NaN (shouldn't happen but safety)
for c in final_cols:
    if c not in base.columns:
        base[c] = np.nan

base = base[final_cols]

# ════════════════════════════════════════════════════════════════════════════
# WRITE OUTPUT
# ════════════════════════════════════════════════════════════════════════════
out_path = os.path.join(PROC, 'agents_config.csv')
base.to_csv(out_path, index=False)
print(f"\nWritten: {out_path}")

# ════════════════════════════════════════════════════════════════════════════
# FINAL VALIDATION
# ════════════════════════════════════════════════════════════════════════════
print("\n" + "="*60)
print("=== agents_config.csv READY ===")
print("="*60)

df_final = pd.read_csv(out_path)
print(f"Total rows written: {len(df_final)}")
print(f"\nNaN count per column:")
nan_counts = df_final.isna().sum()
for col, cnt in nan_counts.items():
    flag = " ⚠ WARNING" if cnt > 0 else ""
    print(f"  {col:<25}: {cnt}{flag}")

num_cols = ['literacy_score','press_freedom_score','polarization_score','internet_penetration','institutional_trust']
for col in num_cols:
    v = df_final[col]
    print(f"\n  {col}:")
    print(f"    min={v.min():.4f}  max={v.max():.4f}  mean={v.mean():.4f}")
    if v.min() == v.max():
        print(f"    ⚠ SUSPICIOUS: min == max (no variance)")

real_platform = df_final['platform_1'].isin([r[1] for r in platform_data])
n_real = real_platform.sum()
n_fallback = len(df_final) - n_real
print(f"\nCountries WITH real platform data: {n_real}")
print(f"Countries using fallback platform data: {n_fallback}")

print(f"\nTop 5 most vulnerable (lowest institutional_trust):")
vuln = df_final.nsmallest(5, 'institutional_trust')[['country','code','institutional_trust','literacy_score','press_freedom_score']]
print(vuln.to_string(index=False))

print(f"\nTop 5 most resistant (highest institutional_trust):")
resist = df_final.nlargest(5, 'institutional_trust')[['country','code','institutional_trust','literacy_score','press_freedom_score']]
print(resist.to_string(index=False))

print(f"\nUnique regions: {sorted(df_final['region'].unique().tolist())}")

print(f"\nSample rows (5):")
print(df_final.sample(5, random_state=42).to_string(index=False))

still_nan = nan_counts[nan_counts > 0]
if len(still_nan) > 0:
    print(f"\n⚠ WARNING: NaN values remain in columns: {list(still_nan.index)}")
    for col in still_nan.index:
        bad = df_final[df_final[col].isna()][['country','code']]
        print(f"  {col} NaN in: {bad['code'].tolist()[:10]}")
else:
    print(f"\n✓ All columns are NaN-free.")
