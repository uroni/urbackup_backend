import { Alert, Button, Select } from "antd";
import { useEffect, useState } from "react";
import { serverTimezoneDef, useMountEffect } from "./App";
import { WizardComponent, WizardState } from "./WizardState";
import produce from "immer";


function SelectTimezone(props: WizardComponent) {

    const [isLoadingAreas, setIsLoadingAreas] = useState(true);
    const [isLoadingCities, setIsLoadingCities] = useState(true);
    const [tzAreas, setTzAreas] = useState<string[]>([]);
    const [tzCities, setTzCities] = useState<string[]>([]);
    const [fetchError, setFetchError] = useState("");
    const [timezoneArea, setTimezoneArea] = useState(props.props.timezoneArea);
    const [timezoneCity, setTimezoneCity] = useState(props.props.timezoneCity);
    const [loadedArea, setLoadedArea] = useState("");

    useMountEffect( () => {
        (async () => {

            let jdata;
            try {
                const resp = await fetch("x?a=get_timezone_areas",
                    {method: "POST"})
                jdata = await resp.json();
            } catch(error) {
                setFetchError("Error while retrieving timezone areas");
                return;
            }

            if(!jdata || !jdata["areas"]) {
                setFetchError("No areas found");
                return;
            }

            let areas : string[] = jdata["areas"];
            areas.unshift(serverTimezoneDef);

            setTzAreas(areas);
            
            if(props.props.timezoneArea===serverTimezoneDef)
            {
                try {
                    const resp = await fetch("x?a=get_timezone_data",
                        {method: "POST"})
                    jdata = await resp.json();
                } catch(error) {
                    setFetchError("Error while retrieving default timezone data");
                    return;
                }

                let tzArea = serverTimezoneDef
                let tzCity = "";

                if(jdata && jdata["ok"] && jdata["timezone"]) {
                    let sd = jdata["timezone"].split("/");
                    if(sd.length>1)
                    {
                        tzArea = sd[0];
                        tzCity = sd[1];
                    }
                    else
                    {
                        tzArea = jdata["timezone"];
                    }
                }

                if(areas.indexOf(tzArea)!==-1)
                {
                    setTimezoneCity(tzCity);
                    setTimezoneArea(tzArea);
                }
            }

            setIsLoadingAreas(false);
        })();
    });

    useEffect(() => {
        (async () => {
            if(timezoneArea===loadedArea)
            {
                setIsLoadingCities(false);
                return;
            }

            if(timezoneArea===serverTimezoneDef || timezoneArea==="UTC")
            {
                setTzCities([]);
                setIsLoadingCities(false);
                setLoadedArea(timezoneArea);
                return;
            }

            setIsLoadingCities(true);
            
            let jdata;
            try {
                const resp = await fetch("x?a=get_timezone_cities",
                    {method: "POST",
                    body: new URLSearchParams({
                        "area": timezoneArea
                    }) } )
                jdata = await resp.json();
            } catch(error) {
                setFetchError("Error while retrieving timezone cities");
                return;
            }

            if(!jdata || !jdata["ok"]) {
                setFetchError("No cities found in area");
                return;
            }

            let cities : string[] = jdata["cities"];

            setTzCities(cities);

            if(cities.indexOf(timezoneCity)===-1)
            {
                if(cities.length>0 && timezoneCity.length===0)
                {
                    setTimezoneCity(cities[0]);
                }
                else
                {
                    setTimezoneCity("");
                }
            }

            setIsLoadingCities(false);
            setLoadedArea(timezoneArea);
        })();
    }, [timezoneArea, timezoneCity, loadedArea]);

    const onAreaSelectChange = (value: string) => {
        setTimezoneArea(value);
        setTimezoneCity("");
    }

    const onCitySelectChange = (value: string) => {
        setTimezoneCity(value);
    }

    const getTzStr = () => {
        if(timezoneCity.length===0)
            return timezoneArea;
        else
            return timezoneArea +"/" + timezoneCity;
    }

    const selectTimezone = async () => {

        if(timezoneArea!==serverTimezoneDef)
        {
            try {
                await fetch("x?a=set_timezone",
                    {method: "POST",
                    body: new URLSearchParams({
                        "tz": getTzStr()
                    })})
            } catch(error) {
                setFetchError("Error setting timezone");
                return;
            }
        }

        props.update(produce(props.props, draft => {
            draft.timezoneArea = timezoneArea;
            draft.timezoneCity = timezoneCity;
            draft.state = WizardState.ServerSearch;
            draft.max_state = draft.state;
        }));
    }

    const skipNext = () => {
        props.update(produce(props.props, draft => {
            draft.state = WizardState.ServerSearch;
            draft.max_state = draft.state;
        }));
    }

    return (<div>
        Please select your timezone: <br /> <br />
            <Select showSearch loading={isLoadingAreas} 
                onChange={onAreaSelectChange} value={timezoneArea} style={{width: "200pt"}}
                optionFilterProp="label">
                {tzAreas.map( (area:string) => {
                    return <Select.Option key={area} value={area} label={area}>
                        {area}
                    </Select.Option>;
                })
                }
            </Select> <br />
            <Select showSearch loading={isLoadingAreas || isLoadingCities} 
                onChange={onCitySelectChange} value={timezoneCity} style={{width: "200pt"}}
                optionFilterProp="label" disabled={tzCities.length===0}>
                {tzCities.map( (city:string) => {
                    return <Select.Option key={city} value={city} label={city}>
                        {city}
                    </Select.Option>;
                })
                }
            </Select>
        { fetchError.length>0 &&
            <Alert message={fetchError} type="error" />
        }
        <br />
        <br />
        <Button onClick={skipNext}>Skip</Button>
        <Button type="primary" htmlType="submit" onClick={selectTimezone} style={{marginLeft:"10pt"}}>
            Select timezone
        </Button>
    </div>)
}

export default SelectTimezone;