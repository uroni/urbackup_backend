export function chunk<T>(input: ReadonlyArray<T>, size: number) {
  return input.reduce<Array<Array<T>>>((arr, item, idx) => {
    return idx % size === 0
      ? [...arr, [item]]
      : [...arr.slice(0, -1), [...arr.slice(-1)[0], item]];
  }, []);
}
