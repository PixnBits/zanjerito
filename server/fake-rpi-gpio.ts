type PinDirection = 'in' | 'out' | 'low' | 'high';

export const promise = {
  setup(channel: number, direction: PinDirection): void {
    console.log('gpio promise setup', channel, direction);
  },
  write(channel: number, direction: PinDirection): void {
    console.log('gpio promise write', channel, direction);
  },

  DIR_HIGH: 'high' as PinDirection,
  DIR_LOW: 'low' as PinDirection,
};
